# Asset-free launcher tests. Never starts SSX, NVIDIA, or any GPU workload.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../launcher/Settings.ps1')
$root=Split-Path $PSScriptRoot -Parent
$checks=0
function Require($Condition,[string]$Message) {
    if (!$Condition) { throw $Message }
    $script:checks++
}
foreach ($scale in 1,2,3) {
    foreach ($hdr in $false,$true) {
        foreach ($aa in 'original','dlaa') {
            $s=Get-SsxDefaults $root
            $s.Scale=$scale; $s.HDR=$hdr; $s.AA=$aa; $s.FG=0; $s.Reflex='disabled'
            $args=@(Get-SsxLaunchArguments $s)
            Require ($args -contains "--resolution_scale=$scale") 'Internal scale changed unexpectedly'
            Require ($args -contains '--d3d12_ssx_fg_enable=false') 'FG off must override saved runtime flags'
            Require ($args -contains "--d3d12_ssx_hdr_preview=$($hdr.ToString().ToLowerInvariant())") 'HDR toggle changed'
            Require ($args -contains '--ssx_reflex_sync=disabled') 'Independent HDR cannot force Reflex on'
            if ($aa -eq 'original') {
                Require ($args -contains '--d3d12_ssx_jitter=false') 'HDR alone must not jitter'
                Require ($args -contains '--d3d12_streamline=false') 'HDR alone cannot need NVIDIA'
            }
        }
    }
}
$s=Get-SsxDefaults $root; $s.Scale=1; $s.Output='1280x720'; $s.FG=3
$args=@(Get-SsxLaunchArguments $s)
Require ($args -contains '--window_width=1280') '720p output must remain selectable with DLAA / FG'
Require ($args -contains '--d3d12_ssx_dlaa=true') 'FG requires DLAA'
$s.AA='original'; $rejected=$false
try { Get-SsxLaunchArguments $s | Out-Null } catch { $rejected=$true }
Require $rejected 'FG without DLAA must be rejected'
$s=Get-SsxDefaults $root; $s.AA='original'; $s.Reflex='boost'
$args=@(Get-SsxLaunchArguments $s)
Require ($args -contains '--d3d12_streamline=true') 'Standalone Reflex needs its NVIDIA runtime'
Require ($args -contains '--d3d12_ssx_dlaa=false') 'Standalone Reflex must not enable DLAA'
$s=Get-SsxDefaults $root; $s.HDR=$true; $s.Peak=400; $s.PaperWhite=400
$rejected=$false
try { Test-SsxSettings $s } catch { $rejected=$true }
Require $rejected 'Invalid brightness relationship must be rejected (no GPU brightness test)'
$s=Get-SsxDefaults $root; $s.GameDirectory='C:\Games\SSX (RF) & snow'; $s.SaveDirectory='C:\Local Saves\SSX'
$previous=[Threading.Thread]::CurrentThread.CurrentCulture
try {
    [Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::GetCultureInfo('de-DE')
    $args=@(Get-SsxLaunchArguments $s)
    Require ($args -contains '--ssx_hdr_highlight_boost=1.25') 'Command-line floats must use invariant culture'
    Require ($args -contains '--game_data_root=C:\Games\SSX (RF) & snow') 'Game path must stay one argument'
} finally { [Threading.Thread]::CurrentThread.CurrentCulture=$previous }
Require ((ConvertTo-SsxWindowsArgument 'C:\path with spaces\') -eq '"C:\path with spaces\\"') 'Trailing slash quoting'
$dir=Join-Path $root ('out/launcher-tests/'+[guid]::NewGuid().ToString('N'))
$path=Join-Path $dir 'settings.json'
Save-SsxSettings $s $path
$loaded=Read-SsxSettings $path $root
Require ($loaded.GameDirectory -eq $s.GameDirectory) 'Paths must round-trip through JSON'
$s.HDR=$true; Save-SsxSettings $s $path
Require ((Read-SsxSettings $path $root).HDR) 'Updated preference was not saved'
Require (Test-Path -LiteralPath "$path.bak") 'Previous preference backup was not retained'
Write-Output "$checks launcher checks passed; no game, FG or HDR brightness run performed."
