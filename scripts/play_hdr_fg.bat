@echo off
setlocal
rem Native HDR + experimental 3x FG, original save, 1000-nit default.
rem TV: append -HDRPeakNits 3000. Use -FGMultiplier 2 for 2x FG.
rem Working HDR without generation: play_hdr_preview.bat.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-hdr-fg\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -HDRPreview -FrameGeneration -EnableExperimentalFG %*
exit /b %errorlevel%
