@echo off
setlocal
rem Uses the development build and original test profile, at the existing 3x scene scale.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -CameraMotion -Diagnostics %*
exit /b %errorlevel%
