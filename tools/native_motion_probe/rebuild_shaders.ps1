[CmdletBinding()]
param([Parameter(Mandatory)][string]$SdkSource, [string]$Fxc = 'fxc.exe')
$ErrorActionPreference = 'Stop'
$shader = Join-Path $SdkSource 'src/ui/shaders/ssx_native_motion.hlsl'
foreach ($stage in @('vs', 'ps')) {
    $header = Join-Path $SdkSource "src/ui/shaders/bytecode/d3d12_5_1/ssx_native_motion_$stage.h"
    & $Fxc /nologo /T "${stage}_5_1" /E "${stage}_main" /O3 /WX /Fh $header /Vn "ssx_native_motion_$stage" $shader
    if ($LASTEXITCODE -ne 0) { throw "FXC $stage failed: $LASTEXITCODE" }
    Get-FileHash -Algorithm SHA256 -LiteralPath $header
}
$header = Join-Path $SdkSource 'src/ui/shaders/bytecode/d3d12_5_1/ssx_native_motion_fast_ps.h'
& $Fxc /nologo /T ps_5_1 /E ps_main /D SSX_NATIVE_STATISTICS=0 /O3 /WX /Fh $header /Vn ssx_native_motion_fast_ps $shader
if ($LASTEXITCODE -ne 0) { throw "FXC fast PS failed: $LASTEXITCODE" }
$lines = Get-Content -LiteralPath $header | ForEach-Object { $_.TrimEnd() }
[IO.File]::WriteAllText($header, ($lines -join "`n") + "`n", [Text.Encoding]::ASCII)
Get-FileHash -Algorithm SHA256 -LiteralPath $header
Get-FileHash -Algorithm SHA256 -LiteralPath $shader
