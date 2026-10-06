@echo off
setlocal
rem Keeps the original profile and existing 3x scene configuration.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -NativeMotion -Diagnostics %*
exit /b %errorlevel%
