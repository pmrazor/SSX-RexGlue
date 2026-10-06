[CmdletBinding()]
param([Parameter(Mandatory)][string]$SdkSource, [Parameter(Mandatory)][string]$Fxc)
$ErrorActionPreference = 'Stop'
$source = Join-Path $SdkSource 'src/ui/shaders/ssx_hdr.cs.hlsl'
$header = Join-Path $SdkSource 'src/ui/shaders/bytecode/d3d12_5_1/ssx_hdr_cs.h'
& $Fxc /nologo /T cs_5_1 /E main /O3 /WX /Vn ssx_hdr_cs /Fh $header $source
if ($LASTEXITCODE -ne 0) { throw "Shader compilation failed: $LASTEXITCODE" }
[IO.File]::WriteAllLines($header, ((Get-Content -LiteralPath $header) | ForEach-Object { $_.TrimEnd() }), [Text.UTF8Encoding]::new($false))
$traceHeader = Join-Path $SdkSource 'src/ui/shaders/bytecode/d3d12_5_1/ssx_hdr_light_trace_cs.h'
& $Fxc /nologo /T cs_5_1 /E main /O3 /WX /D SSX_HDR_LIGHT_TRACE=1 /Vn ssx_hdr_light_trace_cs /Fh $traceHeader $source
if ($LASTEXITCODE -ne 0) { throw "Diagnostic shader compilation failed: $LASTEXITCODE" }
[IO.File]::WriteAllLines($traceHeader, ((Get-Content -LiteralPath $traceHeader) | ForEach-Object { $_.TrimEnd() }), [Text.UTF8Encoding]::new($false))
