[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [Parameter(Mandatory=$true)][string]$Destination
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$BuildDirectory=(Resolve-Path -LiteralPath $BuildDirectory).Path
$RuntimeDirectory=(Resolve-Path -LiteralPath $RuntimeDirectory).Path
$Destination=[IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $Destination) { throw 'Choose a new destination. Existing folders are never overwritten.' }
$files=@('ssx.exe','rexruntime.dll','rexgpu-xenos.dll','SSX.Options.exe','SSX.Options.exe.config')
$vendor=@('sl.interposer.dll','sl.common.dll','sl.dlss.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll',
    'nvngx_dlss.dll','nvngx_dlssg.dll','license.txt','3rd-party-licenses.md','nvngx_dlss.license.txt','reflex.license.txt')
foreach ($file in $files) { if (!(Test-Path -LiteralPath (Join-Path $BuildDirectory $file))) { throw "Build is missing $file" } }
foreach ($file in $vendor) { if (!(Test-Path -LiteralPath (Join-Path $RuntimeDirectory $file))) { throw "Runtime is missing $file" } }
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $BuildDirectory $file) -Destination $Destination }
if (Test-Path -LiteralPath (Join-Path $BuildDirectory 'TracyClient.dll')) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'TracyClient.dll') -Destination $Destination
}
New-Item -ItemType Directory -Path (Join-Path $Destination 'launcher'),(Join-Path $Destination 'streamline') | Out-Null
foreach ($file in 'Settings.ps1','Options.ps1') {
    Copy-Item -LiteralPath (Join-Path $repo "launcher/$file") -Destination (Join-Path $Destination 'launcher')
}
foreach ($file in $vendor) { Copy-Item -LiteralPath (Join-Path $RuntimeDirectory $file) -Destination (Join-Path $Destination 'streamline') }
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination $Destination
Copy-Item -LiteralPath (Join-Path $repo 'docs/experimental-submission.md') -Destination (Join-Path $Destination 'EXPERIMENTAL.md')
Copy-Item -LiteralPath (Join-Path $repo 'scripts/play_options.bat') -Destination $Destination
$manifest=@(Get-ChildItem -LiteralPath $Destination -File -Recurse | ForEach-Object {
    [ordered]@{file=$_.FullName.Substring($Destination.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
})
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'build-manifest.json') -Encoding UTF8
Write-Output "Local experimental build: $Destination. No game assets, generated source, saves, personal preferences, logs or captures were copied."
