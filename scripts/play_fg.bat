@echo off
setlocal
rem Experimental FG with DLAA and Reflex. Defaults to 3x; pass -FGMultiplier 2 for 2x.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch_ssx.ps1" -Profile 4k -ExePath "%~dp0..\out\build\win-amd64-fg\ssx.exe" -UserDataDir "%~dp0..\out\test-user" -FrameGeneration -EnableExperimentalFG %*
exit /b %errorlevel%
