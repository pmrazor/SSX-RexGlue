[CmdletBinding()]
param([Parameter(Mandatory)][string]$SdkSource, [Parameter(Mandatory)][string]$Fxc)
$ErrorActionPreference = 'Stop'
$source = Join-Path $SdkSource 'src/ui/shaders/ssx_scene_color.cs.hlsl'
$header = Join-Path $SdkSource 'src/ui/shaders/bytecode/d3d12_5_1/ssx_scene_color_cs.h'
& $Fxc /nologo /T cs_5_1 /E main /O3 /WX /Vn ssx_scene_color_cs /Fh $header $source
if ($LASTEXITCODE -ne 0) { throw "Shader compilation failed: $LASTEXITCODE" }
