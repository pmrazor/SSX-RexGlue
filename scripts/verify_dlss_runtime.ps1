[CmdletBinding()]
param([string]$RuntimeDirectory)
$ErrorActionPreference = 'Stop'
if (!$RuntimeDirectory) { $RuntimeDirectory = Join-Path $PSScriptRoot '../out/streamline-runtime' }
$dll = (Resolve-Path -LiteralPath (Join-Path $RuntimeDirectory 'nvngx_dlss.dll')).Path
# Verified against NVIDIA/DLSS's current release blob and Streamline 2.14.1 on
# 2026-10-04. Updating the pin requires checking the official release again.
$expected = '3975567B8943C53ACCE397F2B72380092F84F162D00B0D2C7D08A1025C563983'
$hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
$signature = Get-AuthenticodeSignature -LiteralPath $dll
$version = (Get-Item -LiteralPath $dll).VersionInfo.FileVersion
if ($hash -ne $expected -or $signature.Status -ne 'Valid' -or
    $signature.SignerCertificate.Subject -notmatch 'NVIDIA Corporation') {
    throw 'DLSS runtime differs from the verified NVIDIA release or has an invalid NVIDIA signature.'
}
[pscustomobject]@{
    path = $dll
    version = $version
    sha256 = $hash
    signature = $signature.Status.ToString()
    officialGitBlob = '875981984f56e71b4fde6605484ef767dadf9910'
    verifiedDate = '2026-10-04'
} | ConvertTo-Json
