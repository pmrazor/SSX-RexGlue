[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$assembly=[System.Management.Automation.PSObject].Assembly.Location
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
& $compiler /nologo /target:winexe /platform:anycpu /optimize+ "/out:$OutputDirectory/SSX.Options.exe" "/reference:$assembly" /reference:System.Windows.Forms.dll (Join-Path $root 'launcher/Launcher.cs')
if ($LASTEXITCODE -ne 0) { throw 'Options launcher compilation failed.' }
Copy-Item -LiteralPath (Join-Path $root 'launcher/SSX.Options.exe.config') -Destination $OutputDirectory
New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory 'launcher') | Out-Null
foreach ($name in 'Options.ps1','Settings.ps1') {
    Copy-Item -LiteralPath (Join-Path $root "launcher/$name") -Destination (Join-Path $OutputDirectory 'launcher')
}
