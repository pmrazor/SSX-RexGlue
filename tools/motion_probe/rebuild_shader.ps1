[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SdkSource,
    [string]$Fxc = 'fxc.exe'
)
$ErrorActionPreference = 'Stop'
$shader = Join-Path $SdkSource 'src/ui/shaders/ssx_camera_motion.cs.hlsl'
$header = Join-Path $SdkSource 'src/ui/shaders/bytecode/d3d12_5_1/ssx_camera_motion_cs.h'
if (-not (Test-Path -LiteralPath $shader -PathType Leaf)) { throw "Missing shader: $shader" }
& $Fxc /nologo /T cs_5_1 /E main /O3 /WX /Fh $header /Vn ssx_camera_motion_cs $shader
if ($LASTEXITCODE -ne 0) { throw "FXC failed with exit code $LASTEXITCODE" }
Get-FileHash -Algorithm SHA256 -LiteralPath $shader, $header
