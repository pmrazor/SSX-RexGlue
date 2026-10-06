#!/bin/sh
# Linux / macOS (Vulkan; MoltenVK on macOS): 1x internal resolution, uncapped up to 120 fps.
# Expects the extracted game folder next to ssx as "game"; set GAME_DIR otherwise.
DIR="$(cd "$(dirname "$0")" && pwd)"
GAME_DIR="${GAME_DIR:-$DIR/game}"
exec "$DIR/ssx" --game_data_root="$GAME_DIR" --gpu_plugin=xenos --video_mode_refresh_rate=240 --ssx_render_fps=0 --fullscreen=false --window_width=1280 --window_height=720 "$@"
