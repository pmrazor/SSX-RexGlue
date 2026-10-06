@echo off
setlocal
rem Run from the repository scripts directory. Uses the separate development build.
rem Optional: -GameDir "D:\Games\SSX" -Windowed -Keyboard -DryRun
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -FrameInputs -Diagnostics %*
exit /b %errorlevel%
