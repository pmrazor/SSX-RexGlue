[CmdletBinding()]
param(
    [string]$ArchivePath,
    [string]$SdkDirectory,
    [string]$RuntimeDirectory
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$version = '2.14.1'
$expectedHash = '92C4D954631A1710DA86CA3FA8D5034F2B9503838C95FC4AE977AE149319781B'
$url = "https://github.com/NVIDIA-RTX/Streamline/releases/download/v$version/streamline-sdk-v$version.zip"
if (!$ArchivePath) { $ArchivePath = Join-Path $repo "out/vendor/streamline-sdk-v$version.zip" }
if (!$SdkDirectory) { $SdkDirectory = Join-Path $repo "out/vendor/streamline-$version" }
if (!$RuntimeDirectory) { $RuntimeDirectory = Join-Path $repo 'out/streamline-runtime' }
$ArchivePath = [IO.Path]::GetFullPath($ArchivePath)
$SdkDirectory = [IO.Path]::GetFullPath($SdkDirectory)
$RuntimeDirectory = [IO.Path]::GetFullPath($RuntimeDirectory)
if (!(Test-Path -LiteralPath $ArchivePath)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $ArchivePath -Parent) | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $ArchivePath
}
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'Streamline archive hash mismatch. The archive was not extracted or loaded.'
}
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $SdkDirectory -Force
New-Item -ItemType Directory -Force -Path $RuntimeDirectory | Out-Null
$files = @(
    'sl.interposer.dll', 'sl.common.dll', 'sl.dlss.dll', 'sl.dlss_g.dll',
    'sl.reflex.dll', 'sl.pcl.dll', 'nvngx_dlss.dll', 'nvngx_dlssg.dll',
    'nvngx_dlss.license.txt', 'reflex.license.txt'
)
# Stage only the requested features. In particular, do not accidentally initialize
# Ray Reconstruction by handing NGX the entire release binary directory.
$unexpected = Get-ChildItem -LiteralPath $RuntimeDirectory -Filter '*.dll' |
    Where-Object { $_.Name -notin $files }
if ($unexpected) { throw "Runtime directory contains unrelated DLLs: $($unexpected.Name -join ', ')" }
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $SdkDirectory "bin/x64/$file") -Destination $RuntimeDirectory
}
Copy-Item -LiteralPath (Join-Path $SdkDirectory 'license.txt') -Destination $RuntimeDirectory
Copy-Item -LiteralPath (Join-Path $SdkDirectory '3rd-party-licenses.md') -Destination $RuntimeDirectory
$manifest = [ordered]@{
    sdkVersion = $version
    sourceCommit = '2122257e0fce486f91b385aa63b9a09b0a34b363'
    archiveUrl = $url
    archiveSha256 = $expectedHash.ToLowerInvariant()
    files = @(Get-ChildItem -LiteralPath $RuntimeDirectory -Filter '*.dll' |
        Sort-Object Name | ForEach-Object {
            [ordered]@{
                name = $_.Name
                version = $_.VersionInfo.FileVersion
                sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()
            }
        })
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $RuntimeDirectory 'manifest.json')
Write-Output "SDK headers: $SdkDirectory"
Write-Output "Production runtime: $RuntimeDirectory"
Write-Output 'Runtime files prepared. Use the explicit experimental DLAA launcher for gameplay reconstruction; Frame Generation remains off.'
