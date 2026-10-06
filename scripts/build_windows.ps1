[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SdkPath,
    [string]$GameDir,
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')][string]$Configuration = 'Release',
    [ValidateRange(1, 128)][int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
try {
    $SdkPath = (Resolve-Path -LiteralPath $SdkPath).Path
    $codegen = Join-Path $SdkPath 'bin/rexglue.exe'
    $package = Join-Path $SdkPath 'lib/cmake/rexglue'
    if (-not (Test-Path -LiteralPath $codegen -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $package 'rexglueConfig.cmake') -PathType Leaf)) {
        throw 'SdkPath must point to the installed, SSX-patched SDK prefix (out/install/win-amd64).'
    }
    foreach ($tool in @('cmake', 'ninja', 'clang++')) { Get-Command $tool -ErrorAction Stop | Out-Null }
    $localGame = Join-Path $repo 'game'
    if ($GameDir) {
        $GameDir = (Resolve-Path -LiteralPath $GameDir).Path
        if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'default.xex') -PathType Leaf)) {
            throw "default.xex not found in $GameDir."
        }
        if (-not (Test-Path -LiteralPath $localGame)) {
            # Directory junction: assets stay at their original local path and are ignored by Git.
            New-Item -ItemType Junction -Path $localGame -Target $GameDir | Out-Null
        } elseif ([IO.Path]::GetFullPath($localGame) -ne [IO.Path]::GetFullPath($GameDir)) {
            $item = Get-Item -LiteralPath $localGame
            if ($item.LinkType -ne 'Junction' -or $item.Target -ne $GameDir) {
                throw "game already exists; it was left unchanged. Verify it and omit -GameDir, or use its existing target."
            }
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $localGame 'default.xex') -PathType Leaf)) {
        throw 'Provide -GameDir with your extracted SSX folder containing default.xex and the .big archives.'
    }
    Push-Location -LiteralPath $repo
    try {
        & $codegen codegen (Join-Path $repo 'ssx_manifest.toml')
        if ($LASTEXITCODE -ne 0) { throw "Code generation failed ($LASTEXITCODE)." }
        $preset = 'win-amd64-' + $Configuration.ToLowerInvariant()
        & cmake --preset $preset "-Drexglue_DIR=$package" '-DREXSDK_DIR=' "-DCMAKE_PREFIX_PATH=$SdkPath"
        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)." }
        & cmake --build --preset $preset --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
    } finally { Pop-Location }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
