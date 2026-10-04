@echo off
rem 1x internal resolution (1280x720), 120 fps, RTV render path (lowest GPU load and input lag).
rem Expects the extracted game folder next to ssx.exe as "game"; edit GAME_DIR otherwise.
set "GAME_DIR=%~dp0game"
"%~dp0ssx.exe" --game_data_root="%GAME_DIR%" --gpu_plugin=xenos --video_mode_refresh_rate=240 --ssx_render_fps=0 --fullscreen=false --window_width=1280 --window_height=720
