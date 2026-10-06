@echo off
setlocal
rem Optional arguments: -GameDir "D:\Games\SSX" -Windowed -Diagnostics -DryRun
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k %*
exit /b %errorlevel%
