@echo off
rem 2x internal resolution (2560x1440), uncapped up to 120 fps, ROV render path.
rem ROV avoids the EDRAM render target copies that make RTV slow at 2x.
rem Expects the extracted game folder next to ssx.exe as "game"; edit GAME_DIR otherwise.
set "GAME_DIR=%~dp0game"
"%~dp0ssx.exe" --game_data_root="%GAME_DIR%" --gpu_plugin=xenos --video_mode_refresh_rate=240 --ssx_render_fps=0 --resolution_scale=2 --render_target_path_d3d12=rov --fullscreen=false --window_width=1280 --window_height=720
