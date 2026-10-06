@echo off
setlocal
rem HDR checkpoint with original grading, bloom, late world effects and game HUD.
rem Append -HDRSceneOnly for the previous graded-scene diagnostic fallback.
rem Default 1000 nits for the Alienware. TV: append -HDRPeakNits 3000.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-hdr\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -HDRPreview %*
exit /b %errorlevel%
