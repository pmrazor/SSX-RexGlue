@echo off
setlocal
rem Original save, 3x ROV. Jitter is separate opt-in: append -Jitter.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -NativeMotion -SceneColor -Diagnostics %*
exit /b %errorlevel%
