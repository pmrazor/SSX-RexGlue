@echo off
setlocal
rem Render-driven input/Reflex experiment; original save, 3x DLAA, no 30 FPS coupling.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-streamline\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -ReflexSyncMode on -ReflexTimingDirectory "%~dp0..\out\reflex-timing" %*
exit /b %errorlevel%
