# Shared by the Windows options dialog and asset-free command-line checks.
Set-StrictMode -Version 3

function Get-SsxDefaults([string]$Root) {
    $exe = Join-Path $Root 'ssx.exe'
    foreach ($relative in @('ssx.exe', 'out/build/win-amd64-experimental-submit/ssx.exe', 'out/build/win-amd64-experimental/ssx.exe',
            'out/build/win-amd64-hdr-fg/ssx.exe', 'out/build/win-amd64-streamline/ssx.exe',
            'out/build/win-amd64-release/ssx.exe')) {
        $candidate = Join-Path $Root $relative
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $exe = $candidate; break }
    }
    $game = if ($env:SSX_GAME_DIR) { $env:SSX_GAME_DIR } else { Join-Path $Root 'game' }
    if (!$env:SSX_GAME_DIR -and !(Test-Path -LiteralPath (Join-Path $game 'default.xex'))) {
        $repoGame = Join-Path $Root '../../../game'
        if (Test-Path -LiteralPath (Join-Path $repoGame 'default.xex')) { $game=[IO.Path]::GetFullPath($repoGame) }
    }
    $save = if (Test-Path -LiteralPath (Join-Path $Root 'out/test-user')) { Join-Path $Root 'out/test-user' } else { '' }
    if (!$save -and (Test-Path -LiteralPath (Join-Path $Root '../../test-user'))) {
        $save=[IO.Path]::GetFullPath((Join-Path $Root '../../test-user'))
    }
    $sl = Join-Path $Root 'streamline'
    if (!(Test-Path -LiteralPath $sl)) { $sl = Join-Path $Root 'out/streamline-runtime' }
    if (!(Test-Path -LiteralPath $sl) -and (Test-Path -LiteralPath (Join-Path $Root '../../streamline-runtime'))) {
        $sl=[IO.Path]::GetFullPath((Join-Path $Root '../../streamline-runtime'))
    }
    return [ordered]@{
        Version=1; Executable=$exe; GameDirectory=$game; SaveDirectory=$save; StreamlineDirectory=$sl
        Output='3840x2160'; Scale=3; Monitor=0; Windowed=$false; Renderer='auto'
        AA='dlaa'; DLSSPreset='L'; FG=0; Reflex='on'; HDR=$false; Peak=1000; PaperWhite=200; UIWhite=200
        HighlightBoost=1.25; Exposure=0.0; Calibration=$false
        Keyboard=$false; ShowFPS=$true; RenderFPS=0; RefreshRate=240; PresentInterval=2
        PartialTextureReload=$true; GPUProfile=$false; TextureLog=$false
        CommandListDiagnostics=$false; WaitStats=$false; PresentDiagnostics=$false
    }
}

function Get-SsxSchema {
    # Choices use stable serialized values; labels stay separate from renderer flags.
    @(
        @{Key='Output';Label='Output / window size';Group='Display';Choices=@('1280x720','1920x1080','2560x1440','3840x2160')}
        @{Key='Scale';Label='Internal rendering scale';Group='Display';Choices=@('1','2','3');Labels=@('1x (1280 x 720 frame)','2x (2560 x 1440 frame)','3x (3840 x 2160 frame)');Type='int'}
        @{Key='Monitor';Label='Monitor (0 = automatic)';Group='Display';Min=0;Max=16}
        @{Key='Windowed';Label='Windowed mode';Group='Display';Type='bool'}
        @{Key='AA';Label='Antialiasing / upscaling';Group='Rendering';Choices=@('original','dlaa','quality','balanced','performance');Labels=@('Original game MSAA','NVIDIA DLAA','DLSS Quality (experimental / untested)','DLSS Balanced (experimental / untested)','DLSS Performance (experimental / untested)')}
        @{Key='DLSSPreset';Label='DLAA / DLSS model preset';Group='Rendering';Choices=@('default','E','F','J','K','L','M');Labels=@('NVIDIA default (untested)','E - legacy (untested)','F - legacy (untested)','J (untested)','K (untested)','L - existing checkpoint','M (untested)')}
        @{Key='FG';Label='Frame Generation';Group='Rendering';Choices=@('0','2','3','4','5','6');Labels=@('Off','2x output (experimental / untested build)','3x output (existing checkpoint)','4x output (experimental / untested)','5x output (experimental / untested)','6x output (experimental / untested)');Type='int'}
        @{Key='Reflex';Label='NVIDIA Reflex';Group='Rendering';Choices=@('disabled','on','boost');Labels=@('Off','On','On + Boost')}
        @{Key='ShowFPS';Label='Show render FPS, FG FPS and HDR status';Group='Rendering';Type='bool'}
        @{Key='HDR';Label='Enable native HDR (Windows HDR required)';Group='HDR';Type='bool'}
        @{Key='Peak';Label='Peak brightness (nits)';Group='HDR';Min=400;Max=3000}
        @{Key='PaperWhite';Label='Paper white (nits)';Group='HDR';Min=80;Max=500}
        @{Key='UIWhite';Label='HUD / menu white (nits)';Group='HDR';Min=80;Max=500}
        @{Key='HighlightBoost';Label='Highlight strength';Group='HDR';Min=1;Max=4;Decimals=2;Step=0.05}
        @{Key='Exposure';Label='Exposure compensation (EV)';Group='HDR';Min=-5;Max=5;Decimals=2;Step=0.25}
        @{Key='Calibration';Label='Show brightness calibration patches';Group='HDR';Type='bool'}
        @{Key='Renderer';Label='D3D12 render target path';Group='Advanced';Choices=@('auto','rtv','rov');Labels=@('Automatic','RTV','ROV')}
        @{Key='RenderFPS';Label='Render limit (0 = uncapped)';Group='Advanced';Min=0;Max=1000}
        @{Key='RefreshRate';Label='Guest refresh rate (Hz)';Group='Advanced';Min=24;Max=1000}
        @{Key='PresentInterval';Label='Guest flip interval (-1 = game)';Group='Advanced';Min=-1;Max=15}
        @{Key='Keyboard';Label='Enable keyboard / mouse controls';Group='Advanced';Type='bool'}
        @{Key='PartialTextureReload';Label='Partial array / 3D texture reloads';Group='Advanced';Type='bool'}
        @{Key='GPUProfile';Label='GPU per-pass timing';Group='Diagnostics';Type='bool'}
        @{Key='TextureLog';Label='Log texture loads';Group='Diagnostics';Type='bool'}
        @{Key='CommandListDiagnostics';Label='Diagnose invalid command lists';Group='Diagnostics';Type='bool'}
        @{Key='WaitStats';Label='Log thread wait statistics';Group='Diagnostics';Type='bool'}
        @{Key='PresentDiagnostics';Label='Log presentation / scene dimensions';Group='Diagnostics';Type='bool'}
        @{Key='Executable';Label='Game executable (ssx.exe)';Group='Files';Type='file'}
        @{Key='GameDirectory';Label='Extracted game (contains default.xex)';Group='Files';Type='folder'}
        @{Key='SaveDirectory';Label='User data / saves (blank = runtime default)';Group='Files';Type='folder'}
        @{Key='StreamlineDirectory';Label='NVIDIA Streamline runtime folder';Group='Files';Type='folder'}
    )
}

function Read-SsxSettings([string]$Path, [string]$Root) {
    $settings = Get-SsxDefaults $Root
    if (Test-Path -LiteralPath $Path) {
        $loaded = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        if ($loaded.Version -ne 1) { throw 'Unsupported launcher settings version.' }
        foreach ($key in @($settings.Keys)) {
            if ($loaded.PSObject.Properties.Name -contains $key) { $settings[$key] = $loaded.$key }
        }
    }
    return $settings
}

function Test-SsxSettings($Settings) {
    foreach ($field in (Get-SsxSchema)) {
        $value = $Settings[$field.Key]
        if ($field.ContainsKey('Type') -and $field.Type -eq 'bool') {
            if ($value -isnot [bool]) { throw "$($field.Label) must be true or false." }
        } elseif ($field.ContainsKey('Choices')) {
            if ([string]$value -notin $field.Choices) { throw "Invalid $($field.Label)." }
        } elseif ($field.ContainsKey('Min')) {
            $number = 0.0
            if (![double]::TryParse([string]$value, [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
                [double]::IsNaN($number) -or [double]::IsInfinity($number) -or
                $number -lt $field.Min -or $number -gt $field.Max) { throw "Invalid $($field.Label)." }
            if (!$field.ContainsKey('Decimals') -and $number -ne [math]::Truncate($number)) {
                throw "$($field.Label) requires a whole number."
            }
        } elseif ($value -isnot [string] -or $value -match '[\x00-\x1f"]') {
            throw "Invalid path in $($field.Label)."
        }
    }
    if ($Settings.FG -ne 0 -and $Settings.AA -ne 'dlaa') { throw 'Frame Generation requires NVIDIA DLAA. DLAA can also be used with FG off.' }
    if (($Settings.AA -ne 'original' -or $Settings.HDR) -and $Settings.Renderer -eq 'rtv') { throw 'DLAA / DLSS and HDR composition use the ROV renderer. Select Automatic or ROV.' }
    if ($Settings.HDR -and ($Settings.PaperWhite -ge $Settings.Peak -or $Settings.UIWhite -gt $Settings.Peak)) {
        throw 'Paper white must be below peak brightness; HUD white must not exceed it.'
    }
    if ($Settings.Calibration -and !$Settings.HDR) { throw 'Enable HDR to show brightness calibration patches.' }
}

function Save-SsxSettings($Settings, [string]$Path) {
    Test-SsxSettings $Settings
    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($Path))
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    # Write beside the destination, then atomically replace so interrupted saves
    # cannot truncate the existing preferences. Preserve the previous version.
    $temp = Join-Path $parent ([IO.Path]::GetRandomFileName())
    try {
        [IO.File]::WriteAllText($temp, ($Settings | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temp, $Path, "$Path.bak") }
        else { [IO.File]::Move($temp, $Path) }
    } finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp } }
}

function Get-SsxLaunchArguments($Settings) {
    Test-SsxSettings $Settings
    $s = $Settings
    $temporal = $s.AA -ne 'original'
    $fg = $s.FG -ne 0
    # FG needs Reflex timing internally, even when standalone Reflex is off.
    $reflex = if ($fg -and $s.Reflex -eq 'disabled') { 'on' } else { $s.Reflex }
    $streamline = $temporal -or $reflex -ne 'disabled'
    $renderer = $s.Renderer
    if ($renderer -eq 'auto') { $renderer = if ($temporal -or $s.HDR -or $s.Scale -gt 1) { 'rov' } else { 'rtv' } }
    $size = $s.Output.Split('x')
    $flags = [ordered]@{
        game_data_root=[IO.Path]::GetFullPath($s.GameDirectory); gpu_plugin='xenos'
        resolution_scale=$s.Scale; draw_resolution_scale_x=$s.Scale; draw_resolution_scale_y=$s.Scale
        render_target_path_d3d12=$renderer; fullscreen=(!$s.Windowed)
        window_width=$size[0]; window_height=$size[1]; monitor=$s.Monitor; present_effect='bilinear'
        video_mode_refresh_rate=$s.RefreshRate; ssx_present_interval=$s.PresentInterval; ssx_render_fps=$s.RenderFPS
        ssx_show_fps=$s.ShowFPS; mnk_mode=$s.Keyboard; texture_partial_array_reload=$s.PartialTextureReload
        d3d12_gpu_profile=$s.GPUProfile; d3d12_log_texture_loads=$s.TextureLog
        d3d12_diagnose_invalid_command_lists=$s.CommandListDiagnostics; log_wait_stats=$s.WaitStats
        d3d12_log_presenter=$s.PresentDiagnostics; d3d12_log_swap=$s.PresentDiagnostics
        ssx_log_scene_size=$s.PresentDiagnostics; ssx_scene_720p=$false
        d3d12_ssx_frame_inputs=($temporal -or $s.HDR); d3d12_ssx_scene_color=($temporal -or $s.HDR)
        d3d12_ssx_jitter=$temporal; d3d12_ssx_camera_motion=$temporal; d3d12_ssx_native_motion=$temporal
        d3d12_ssx_dlaa=($s.AA -eq 'dlaa'); d3d12_ssx_quality=($s.AA -in @('quality','balanced','performance')); d3d12_streamline=$streamline
        d3d12_ssx_dlss_mode=$(if ($s.AA -in @('quality','balanced','performance')) { $s.AA } else { 'quality' })
        d3d12_ssx_dlss_preset=$s.DLSSPreset
        d3d12_ssx_frame_generation=$fg; d3d12_ssx_fg_enable=$fg; d3d12_ssx_fg_inputs=$fg
        d3d12_ssx_fg_multiplier=$(if ($fg) { $s.FG } else { 3 })
        ssx_reflex_sync=$reflex; ssx_reflex_mode=$(if ($reflex -eq 'disabled') { 'off' } else { $reflex })
        ssx_reflex_frame_driven=$true; ssx_frame_token_handoff=$false; ssx_frame_timing_trace=$false
        ssx_frame_timing_capture=''; d3d12_ssx_native_capture_path=''; d3d12_ssx_fg_capture_path=''
        d3d12_ssx_hdr_preview=$s.HDR; d3d12_ssx_hdr_layers=$s.HDR; d3d12_ssx_hdr_light_trace=$false
        ssx_hdr_peak_nits=$s.Peak; ssx_hdr_paper_white_nits=$s.PaperWhite; ssx_hdr_ui_nits=$s.UIWhite
        ssx_hdr_exposure_ev=$s.Exposure; ssx_hdr_highlight_boost=$s.HighlightBoost
        ssx_hdr_calibration=$s.Calibration; d3d12_debug=$false
    }
    if ($s.SaveDirectory) { $flags.user_data_root = [IO.Path]::GetFullPath($s.SaveDirectory) }
    if ($streamline) { $flags.d3d12_streamline_path = [IO.Path]::GetFullPath($s.StreamlineDirectory) }
    foreach ($entry in $flags.GetEnumerator()) {
        $value = $entry.Value
        if ($value -is [bool]) { $value = $value.ToString().ToLowerInvariant() }
        elseif ($value -is [IFormattable]) { $value = $value.ToString($null, [Globalization.CultureInfo]::InvariantCulture) }
        "--$($entry.Key)=$value"
    }
}

function ConvertTo-SsxWindowsArgument([string]$Value) {
    # Windows CRT quoting, including trailing slashes and embedded quotes.
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}

function Start-SsxGame($Settings) {
    $arguments = @(Get-SsxLaunchArguments $Settings)
    if (!(Test-Path -LiteralPath $Settings.Executable -PathType Leaf)) { throw 'Select a built ssx.exe on the Files tab.' }
    if (!(Test-Path -LiteralPath (Join-Path $Settings.GameDirectory 'default.xex') -PathType Leaf)) {
        throw 'Select your extracted game folder containing default.xex on the Files tab.'
    }
    if ($Settings.AA -ne 'original' -or $Settings.Reflex -ne 'disabled' -or $Settings.FG -ne 0) {
        $required = @('sl.interposer.dll','sl.common.dll','sl.reflex.dll','sl.pcl.dll')
        if ($Settings.AA -ne 'original') { $required += 'sl.dlss.dll','nvngx_dlss.dll' }
        if ($Settings.FG -ne 0) { $required += 'sl.dlss_g.dll','nvngx_dlssg.dll' }
        foreach ($file in $required) {
            if (!(Test-Path -LiteralPath (Join-Path $Settings.StreamlineDirectory $file) -PathType Leaf)) {
                throw "Missing NVIDIA runtime: $file. Select the prepared Streamline folder on the Files tab."
            }
        }
    }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = [IO.Path]::GetFullPath($Settings.Executable)
    $start.WorkingDirectory = Split-Path -Parent $start.FileName
    $start.UseShellExecute = $false
    $start.Arguments = ($arguments | ForEach-Object { ConvertTo-SsxWindowsArgument $_ }) -join ' '
    [Diagnostics.Process]::Start($start)
}
