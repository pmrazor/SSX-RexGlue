[CmdletBinding()]
param(
    [ValidateSet('1x', 'qhd', '4k')][string]$Profile = '4k',
    [string]$GameDir = $env:SSX_GAME_DIR,
    [string]$ExePath,
    [string]$UserDataDir,
    [ValidateRange(0, 16)][int]$Monitor = 0,
    [switch]$Windowed,
    [switch]$Keyboard,
    [switch]$Diagnostics,
    [switch]$GpuProfile,
    [switch]$ExperimentalScene720p,
    [switch]$FrameInputs,
    [switch]$CameraMotion,
    [switch]$NativeMotion,
    [switch]$SceneColor,
    [switch]$Jitter,
    [switch]$DLAA,
    [switch]$Quality,
    [switch]$FGInputs,
    [switch]$FrameGeneration,
    [switch]$EnableExperimentalFG,
    [switch]$HDRPreview,
    [switch]$HDRWithoutDLAA,
    [switch]$HDRSceneOnly,
    [ValidateRange(400, 3000)][int]$HDRPeakNits = 1000,
    [ValidateRange(80, 500)][int]$HDRPaperWhiteNits = 200,
    [ValidateRange(80, 500)][int]$HDRUINits = 200,
    [ValidateRange(-5, 5)][double]$HDRExposureEV = 0,
    [ValidateRange(1, 4)][double]$HDRHighlightBoost = 1.25,
    [switch]$HDRCalibration,
    [ValidateRange(2, 6)][int]$FGMultiplier = 3,
    [ValidateSet('default','E','F','J','K','L','M')][string]$DLSSPreset = 'L',
    [ValidateSet('quality','balanced','performance')][string]$DLSSMode = 'quality',
    [switch]$FrameTokens,
    [ValidateSet('disabled', 'off', 'on', 'boost')][string]$ReflexSyncMode = 'disabled',
    [switch]$ReflexCoupledPhysics,
    [string]$FGCaptureDirectory,
    [string]$ReflexTimingDirectory,
    [string]$StreamlineDirectory,
    [string]$NativeCaptureDirectory,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
try {
    if ($HDRPreview) {
        if ($FrameGeneration -and $HDRSceneOnly) {
            throw 'HDR Frame Generation requires full bloom/world/HUD composition; omit -HDRSceneOnly.'
        }
        if ($HDRPaperWhiteNits -ge $HDRPeakNits -or $HDRUINits -gt $HDRPeakNits) {
            throw 'HDR paper white must be below peak; UI white must not exceed peak.'
        }
        if (!$HDRWithoutDLAA -and !$Quality) { $DLAA = [switch]$true }
        $FrameInputs = [switch]$true
        $SceneColor = [switch]$true
    }
    if ($HDRWithoutDLAA -and (!$HDRPreview -or $DLAA -or $Quality -or $FrameGeneration)) {
        throw '-HDRWithoutDLAA requires HDR with DLAA, Quality and FG off.'
    }
    if ($HDRCalibration -and !$HDRPreview) { throw '-HDRCalibration requires -HDRPreview.' }
    if ($HDRSceneOnly -and !$HDRPreview) { throw '-HDRSceneOnly requires -HDRPreview.' }
    if ($EnableExperimentalFG -and !$FrameGeneration) { throw '-EnableExperimentalFG requires -FrameGeneration.' }
    if ($FrameGeneration) {
        if ($Quality) { throw 'Experimental FG requires DLAA.' }
        $FGInputs = [switch]$true
        $ReflexSyncMode = 'on'
    }
    if ($ReflexSyncMode -ne 'disabled' -and !$HDRWithoutDLAA) { $DLAA = [switch]$true }
    if ($FrameTokens) { $DLAA = [switch]$true }
    if ($FGInputs) {
        if ($Quality) { throw '-FGInputs currently requires DLAA.' }
        $DLAA = [switch]$true
    }
    if ($DLAA -and $Quality) { throw 'Choose -DLAA or -Quality, not both.' }
    if ($DLAA -or $Quality) {
        $FrameInputs = $true; $CameraMotion = $true; $NativeMotion = $true
        $SceneColor = $true; $Jitter = $true
    }
    if (-not $ExePath) {
        $ExePath = Join-Path $PSScriptRoot 'ssx.exe'
        if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
            $preset = if ($FrameInputs -or $CameraMotion -or $NativeMotion -or $SceneColor -or $Jitter) { 'win-amd64-streamline' } else { 'win-amd64-release' }
            $ExePath = Join-Path $PSScriptRoot "../out/build/$preset/ssx.exe"
        }
    }
    $ExePath = [IO.Path]::GetFullPath($ExePath)
    if (-not $GameDir) {
        $GameDir = Join-Path (Split-Path -Parent $ExePath) 'game'
        # Both scripts/ and the staged out/build/<preset>/ launcher find the
        # repository's ignored game junction, while portable layouts use game/.
        $gameCandidates = @($GameDir, (Join-Path $PSScriptRoot '../game'),
                            (Join-Path $PSScriptRoot '../../../game'))
        foreach ($candidate in $gameCandidates) {
            if (Test-Path -LiteralPath (Join-Path $candidate 'default.xex') -PathType Leaf) {
                $GameDir = $candidate
                break
            }
        }
    }
    $GameDir = [IO.Path]::GetFullPath($GameDir)
    $scale = @{ '1x' = 1; 'qhd' = 2; '4k' = 3 }[$Profile]
    $renderPath = if ($scale -eq 1 -and !$HDRPreview -and !$DLAA -and !$Quality) { 'rtv' } else { 'rov' }
    $width = 1280 * $scale
    $height = 720 * $scale
    $fullscreen = (-not $Windowed.IsPresent).ToString().ToLowerInvariant()
    $launchArgs = @(
        "--game_data_root=$GameDir", '--gpu_plugin=xenos',
        "--resolution_scale=$scale", "--draw_resolution_scale_x=$scale",
        "--draw_resolution_scale_y=$scale", "--render_target_path_d3d12=$renderPath",
        "--fullscreen=$fullscreen", "--window_width=$width", "--window_height=$height",
        "--monitor=$Monitor",
        '--present_effect=bilinear', '--video_mode_refresh_rate=240',
        '--ssx_present_interval=2', '--ssx_render_fps=0', '--ssx_show_fps=true'
        "--d3d12_ssx_dlss_preset=$DLSSPreset", "--d3d12_ssx_dlss_mode=$DLSSMode"
    )
    $launchArgs += '--d3d12_log_swap=' + $Diagnostics.IsPresent.ToString().ToLowerInvariant()
    $launchArgs += '--d3d12_log_presenter=' + $Diagnostics.IsPresent.ToString().ToLowerInvariant()
    if ($Diagnostics) {
        # Requires patches/rexglue-sdk-present-diagnostics.patch after the SSX fixes.
        $launchArgs += '--ssx_log_scene_size=true'
    }
    # Explicit profiling is separate from verbose diagnostics. Preserve the
    # normal Reflex timing run without timestamp instrumentation unless asked.
    $profileGpu = $GpuProfile.IsPresent -or ($Diagnostics.IsPresent -and $ReflexSyncMode -eq 'disabled')
    $launchArgs += '--d3d12_gpu_profile=' + $profileGpu.ToString().ToLowerInvariant()
    # Explicit false preserves the normal fallback even if a local TOML enabled it.
    $launchArgs += '--ssx_scene_720p=' + $ExperimentalScene720p.IsPresent.ToString().ToLowerInvariant()
    # Explicitly disable the other reconstruction mode even if enabled in TOML.
    if ($FrameInputs -or $CameraMotion -or $NativeMotion -or $SceneColor -or $Jitter) {
        $launchArgs += '--d3d12_ssx_quality=' + $Quality.IsPresent.ToString().ToLowerInvariant()
    }
    if ($ExperimentalScene720p) {
        Write-Host 'Experimental 1280x720 base scene requested. Gameplay validation is required.'
    }
    if ($UserDataDir) {
        $launchArgs += '--user_data_root=' + [IO.Path]::GetFullPath($UserDataDir)
    }
    if ($Keyboard) { $launchArgs += '--mnk_mode=true' }
    if ($FrameInputs -or $CameraMotion -or $NativeMotion -or $SceneColor -or $Jitter) {
        $launchArgs += '--d3d12_ssx_frame_inputs=true'
        $launchArgs += '--d3d12_ssx_scene_color=' + $SceneColor.IsPresent.ToString().ToLowerInvariant()
        $launchArgs += '--d3d12_ssx_jitter=' + $Jitter.IsPresent.ToString().ToLowerInvariant()
        $launchArgs += '--d3d12_ssx_camera_motion=' + ($CameraMotion.IsPresent -or $NativeMotion.IsPresent).ToString().ToLowerInvariant()
        $launchArgs += '--d3d12_ssx_native_motion=' + $NativeMotion.IsPresent.ToString().ToLowerInvariant()
        $launchArgs += '--d3d12_ssx_dlaa=' + $DLAA.IsPresent.ToString().ToLowerInvariant()
        $launchArgs += '--d3d12_streamline=' + ($DLAA.IsPresent -or $Quality.IsPresent -or $ReflexSyncMode -ne 'disabled').ToString().ToLowerInvariant()
        if (!$DLAA -and !$Quality) { Write-Host 'SSX diagnostic input collection enabled; no DLSS evaluation.' }
    }
    if ($DLAA -or $Quality -or $HDRPreview) {
        if ($HDRPreview -or $PSBoundParameters.ContainsKey('HDRPreview')) {
            $launchArgs += '--d3d12_ssx_hdr_preview=' + $HDRPreview.IsPresent.ToString().ToLowerInvariant()
        }
        if ($HDRPreview) {
            $launchArgs += '--d3d12_ssx_hdr_layers=' + (!$HDRSceneOnly).ToString().ToLowerInvariant()
            $launchArgs += "--ssx_hdr_peak_nits=$HDRPeakNits", "--ssx_hdr_paper_white_nits=$HDRPaperWhiteNits", "--ssx_hdr_ui_nits=$HDRUINits"
            $launchArgs += '--ssx_hdr_exposure_ev=' + $HDRExposureEV.ToString([Globalization.CultureInfo]::InvariantCulture)
            $launchArgs += '--ssx_hdr_highlight_boost=' + $HDRHighlightBoost.ToString([Globalization.CultureInfo]::InvariantCulture)
            $launchArgs += '--ssx_hdr_calibration=' + $HDRCalibration.IsPresent.ToString().ToLowerInvariant()
            Write-Host "HDR native-scene PREVIEW: $HDRPeakNits nit peak / $HDRPaperWhiteNits paper white / $HDRUINits UI. Windows HDR required."
            Write-Host "Original grading enabled. Bloom/world/game-HUD composition: $(!$HDRSceneOnly). Menus/pause use SDR mapped into HDR. FG requested: $($EnableExperimentalFG.IsPresent)."
        }
        $launchArgs += '--d3d12_ssx_frame_generation=' + $FrameGeneration.IsPresent.ToString().ToLowerInvariant()
        $launchArgs += '--d3d12_ssx_fg_enable=' + $EnableExperimentalFG.IsPresent.ToString().ToLowerInvariant()
        $launchArgs += "--d3d12_ssx_fg_multiplier=$FGMultiplier"
        # Validation is covered by the GPU probe, not enabled for gameplay.
        $launchArgs += '--d3d12_debug=false'
        $launchArgs += "--ssx_reflex_sync=$ReflexSyncMode"
        $launchArgs += '--ssx_reflex_frame_driven=' + (!$ReflexCoupledPhysics.IsPresent).ToString().ToLowerInvariant()
        if ($ReflexSyncMode -ne 'disabled') {
            $launchArgs += "--ssx_reflex_mode=$ReflexSyncMode"
            if ($ReflexCoupledPhysics) {
                Write-Host "Diagnostic coupled Reflex $ReflexSyncMode`: 30 FPS. FG off."
            } else {
                Write-Host "Experimental render-driven Reflex $ReflexSyncMode`: fresh input per rendered update; physics cadence unchanged."
            }
        }
        if (($DLAA -or $Quality -or $ReflexSyncMode -ne 'disabled') -and !$StreamlineDirectory) {
            $candidates = @((Join-Path $PSScriptRoot '../out/streamline-runtime'),
                             (Join-Path $PSScriptRoot '../../streamline-runtime'))
            $StreamlineDirectory = $candidates[0]
            foreach ($candidate in $candidates) {
                if (Test-Path -LiteralPath (Join-Path $candidate 'sl.interposer.dll')) {
                    $StreamlineDirectory = $candidate; break
                }
            }
        }
        if ($StreamlineDirectory) { $launchArgs += '--d3d12_streamline_path=' + [IO.Path]::GetFullPath($StreamlineDirectory) }
        if ($Quality) {
            Write-Host "Experimental DLSS $DLSSMode / preset $DLSSPreset at ${scale}x post/HUD. Integer-raster inputs may be resized; gameplay untested, FG off."
        } elseif ($DLAA) {
            Write-Host 'Experimental DLAA before the SSX tone map. Existing scene resolution; single-sample inputs only.'
        }
    }
    if ($SceneColor -and !$DLAA -and !$Quality -and !$HDRPreview) {
        Write-Host 'Experimental pre-tone-map color and exposure diagnostics enabled; guest presentation is unchanged.'
    }
    if ($Jitter -and !$DLAA -and !$Quality) {
        Write-Host 'Experimental partial scene jitter enabled. Shimmer is expected without reconstruction; no DLSS evaluation.'
    }
    if ($NativeMotion) {
        Write-Host 'Experimental native rider/board motion enabled. Mesh identity and coverage still require gameplay validation.'
    } elseif ($CameraMotion) {
        Write-Host 'Experimental camera-only motion vectors enabled. Rider/object motion is not included.'
    }
    if ($NativeMotion) {
        # Do not inherit a stale capture.request from an earlier diagnostic run.
        $capturePath = if ($NativeCaptureDirectory) { [IO.Path]::GetFullPath($NativeCaptureDirectory) } else { '' }
        $launchArgs += '--d3d12_ssx_native_capture_path=' + $capturePath
    }
    if ($NativeCaptureDirectory) {
        if (-not $NativeMotion) { throw '-NativeCaptureDirectory requires -NativeMotion.' }
        Write-Host 'Create capture.request in the capture directory during gameplay to save one native-motion frame locally.'
    }
    if ($DLAA -or $Quality) {
        $launchArgs += '--d3d12_ssx_fg_inputs=' + $FGInputs.IsPresent.ToString().ToLowerInvariant()
        $capturePath = if ($FGCaptureDirectory) { [IO.Path]::GetFullPath($FGCaptureDirectory) } else { '' }
        $launchArgs += '--d3d12_ssx_fg_capture_path=' + $capturePath
    }
    if ($FGInputs) {
        $launchArgs += '--ssx_frame_timing_trace=true'
        if ($FrameGeneration) {
            Write-Host ("Experimental ${FGMultiplier}x FG presenter loaded. Generation enabled: " + $EnableExperimentalFG.IsPresent)
            Write-Host 'Requires validated HUD-free color and unpaused race inputs. UI alpha is not integrated.'
        } else { Write-Host 'FG input diagnostics enabled with DLAA. Gameplay Frame Generation is not enabled.' }
    } elseif ($FGCaptureDirectory) { throw '-FGCaptureDirectory requires -FGInputs.' }
    Write-Host "Requested profile: $Profile, ${width}x${height} for a 1280x720 guest frame, $renderPath."
    Write-Host 'Guest flip ceiling: 120 Hz. Actual render resolution and performance require runtime verification.'
    if ($ReflexTimingDirectory) {
        $launchArgs += '--ssx_frame_timing_capture=' + [IO.Path]::GetFullPath($ReflexTimingDirectory)
        if (!$DryRun) {
            New-Item -ItemType Directory -Path $ReflexTimingDirectory -Force | Out-Null
        }
    }
    if ($FrameTokens) {
        $launchArgs += '--ssx_frame_token_handoff=true'
        Write-Host 'Experimental NVIDIA token handoff enabled; gameplay Reflex pacing and FG remain off.'
    }
    if ($DryRun) {
        # Does not require game assets, an executable, or a writable game directory.
        [pscustomobject]@{ Executable = $ExePath; Arguments = $launchArgs } | ConvertTo-Json -Depth 3
        exit 0
    }
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        throw "ssx.exe not found at $ExePath. Build first or pass -ExePath."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'default.xex') -PathType Leaf)) {
        throw "default.xex not found in $GameDir. Pass -GameDir or set SSX_GAME_DIR to your extracted game folder."
    }
    Push-Location -LiteralPath (Split-Path -Parent $ExePath)
    try { & $ExePath @launchArgs; $gameExit = $LASTEXITCODE } finally { Pop-Location }
    exit $gameExit
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
