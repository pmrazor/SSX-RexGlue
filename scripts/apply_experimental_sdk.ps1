[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SdkSource)
$ErrorActionPreference='Stop'
$patchRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../patches'))
$series=Get-Content -LiteralPath (Join-Path $patchRoot 'experimental-series.json') -Raw | ConvertFrom-Json
$SdkSource=(Resolve-Path -LiteralPath $SdkSource).Path
function Invoke-SdkGit([string[]]$Arguments) {
    & git -c "safe.directory=$SdkSource" -C $SdkSource @Arguments
    if ($LASTEXITCODE -ne 0) { throw "SDK git command failed: $($Arguments -join ' '). Existing work is preserved; inspect git status before retrying." }
}
$head=Invoke-SdkGit -Arguments @('rev-parse','HEAD')
if ($head -ne $series.sdkBase) { throw "Start with a separate clean SDK checkout at $($series.sdkBase). This script does not reset existing work." }
if (Invoke-SdkGit -Arguments @('status','--porcelain','--untracked-files=normal')) { throw 'SDK has local changes. Use a fresh checkout; existing changes were left alone.' }
if ((Get-FileHash -LiteralPath (Join-Path $patchRoot $series.upstreamFixes) -Algorithm SHA256).Hash -ne $series.upstreamFixesSha256) { throw 'Original SSX fixes hash mismatch.' }
foreach ($entry in $series.patches) {
    $path=Join-Path $patchRoot $entry.file
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Patch hash mismatch: $($entry.file)" }
}
Invoke-SdkGit -Arguments @('am',(Join-Path $patchRoot $series.upstreamFixes))
foreach ($entry in $series.patches) {
    $path=Join-Path $patchRoot $entry.file
    Invoke-SdkGit -Arguments @('apply','--check',$path)
    Invoke-SdkGit -Arguments @('apply',$path)
}
Write-Output 'Experimental SDK patches applied. Build with Streamline enabled; see docs/experimental-submission.md.'
