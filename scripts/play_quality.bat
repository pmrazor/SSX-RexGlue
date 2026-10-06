@echo off
setlocal
rem Original save. Native 2x scene -> DLSS Quality/L -> 3x tone map and HUD.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -Quality -Diagnostics %*
exit /b %errorlevel%
