@echo off
setlocal
rem DLAA plus post-effects/HUD and scheduler diagnostics. Gameplay FG stays off.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -FGInputs -FGCaptureDirectory "%~dp0..\out\fg-inputs" -Diagnostics %*
exit /b %errorlevel%
