[CmdletBinding()]
param(
    [string]$Probe,
    [string]$RuntimeDirectory,
    [string]$OutputDirectory,
    [string]$ColorReplayFile,
    [switch]$FrameGeneration,
    [switch]$FrameGenerationOnly,
    [ValidateSet(2, 3)][int]$FGMultiplier = 2,
    [switch]$RenderTokensOnly,
    [ValidateRange(10, 600)][int]$TimeoutSeconds = 90
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$Probe) { $Probe = Join-Path $repo 'out/build/streamline-probe/ssx_streamline_probe.exe' }
if (!$RuntimeDirectory) { $RuntimeDirectory = Join-Path $repo 'out/streamline-runtime' }
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repo ('out/streamline-validation/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$Probe = (Resolve-Path -LiteralPath $Probe).Path
$RuntimeDirectory = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
& (Join-Path $PSScriptRoot 'verify_dlss_runtime.ps1') -RuntimeDirectory $RuntimeDirectory |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'dlss-runtime.json') -Encoding UTF8

# Run serially, with separate logs and a deadline for each child we own. This
# runner never attaches to or stops a game. No LLDB dependency and no SDK bypass.
function Invoke-ProbeCase([string]$Name, [string[]]$ProbeArguments) {
    $directory = Join-Path $OutputDirectory $Name
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $arguments = ($ProbeArguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $child = Start-Process -FilePath $Probe -ArgumentList $arguments -WorkingDirectory $directory `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $directory 'stdout.txt') `
        -RedirectStandardError (Join-Path $directory 'stderr.txt')
    try {
        # Windows PowerShell 5.1 must retain the process handle before waiting;
        # otherwise a child that exits quickly may expose a null ExitCode.
        $null = $child.Handle
        if (!$child.WaitForExit($TimeoutSeconds * 1000)) {
            # A failed test is explicitly a failure, never a successful shutdown.
            $child.Kill()
            $child.WaitForExit()
            throw "$Name timed out. Logs: $directory. If restricted execution stalls in NVIDIA telemetry teardown, rerun this local probe on the normal host; do not bypass slShutdown or change system telemetry settings."
        }
        $child.WaitForExit()
        $watch.Stop()
        if ($child.ExitCode -ne 0) { throw "$Name failed (exit $($child.ExitCode)). Logs: $directory" }
        Write-Host ("PASS {0} ({1:N2}s) - {2}" -f $Name, $watch.Elapsed.TotalSeconds, $directory)
    } finally {
        $child.Dispose()
    }
}

if (!$FrameGenerationOnly -and !$RenderTokensOnly) {
Invoke-ProbeCase 'sr-evaluation' @($RuntimeDirectory, '--debug', '--evaluate-smoke')
$srLog = Get-Content -LiteralPath (Join-Path $OutputDirectory 'sr-evaluation/streamline-probe.log') -Raw
if ($srLog -notmatch 'NgxDltss[^\r\n]*\(Quality\) Using App hint Preset L' -or
    $srLog -notmatch 'NgxDltss[^\r\n]*\(DLAA\) Using App hint Preset L') {
    throw 'NGX logs did not confirm effective Preset L for both Quality and DLAA.'
}
Invoke-ProbeCase 'disabled' @('--disabled', '--debug')
$missing = Join-Path $OutputDirectory ('missing-sdk-' + [guid]::NewGuid().ToString('N'))
Invoke-ProbeCase 'missing-sdk' @($missing, '--expect-fallback', '--debug')
}
if ($ColorReplayFile) {
    $ColorReplayFile = (Resolve-Path -LiteralPath $ColorReplayFile).Path
    Invoke-ProbeCase 'color-replay' @($RuntimeDirectory, '--debug', '--color-replay', $ColorReplayFile)
}
if ($FrameGeneration -or $FrameGenerationOnly) {
    $fgCase = if ($FGMultiplier -eq 3) { '--fg-smoke-3x' } else { '--fg-smoke' }
    Invoke-ProbeCase 'frame-generation' @($RuntimeDirectory, '--debug', $fgCase)
}
if ($RenderTokensOnly) {
    Invoke-ProbeCase 'render-tokens' @($RuntimeDirectory, '--debug', '--token-smoke')
}
Write-Host 'Requested GPU probes completed with clean process exit. These analytic tests do not verify SSX gameplay image quality.'
