@echo off
setlocal
rem Original profile, existing 3x scene. Experimental same-resolution DLSS AA.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -DLAA -Diagnostics %*
exit /b %errorlevel%
