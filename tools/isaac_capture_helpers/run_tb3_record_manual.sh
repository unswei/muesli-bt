#!/usr/bin/env bash
set -euo pipefail

# Auxiliary capture wrapper for remote Isaac hosts.
# Starts a fixed Xvfb display, runs the TurtleBot3 scene script, and attempts to
# record the viewport with ffmpeg.

source /home/deploy/env_isaaclab/bin/activate
export OMNI_KIT_ACCEPT_EULA=YES

rm -f \
  /home/deploy/tb3_display.mp4 \
  /home/deploy/show_tb3_scene.log \
  /home/deploy/ffmpeg_tb3.log \
  /home/deploy/xvfb_tb3.log

Xvfb :88 -screen 0 1280x720x24 > /home/deploy/xvfb_tb3.log 2>&1 &
XVFB_PID=$!
trap 'kill $XVFB_PID >/dev/null 2>&1 || true' EXIT

sleep 2
export DISPLAY=:88

python tools/isaac_capture_helpers/show_tb3_scene.py > /home/deploy/show_tb3_scene.log 2>&1 &
APP_PID=$!

sleep 18

ffmpeg -y \
  -video_size 1280x720 \
  -framerate 24 \
  -f x11grab \
  -i :88 \
  -t 10 \
  -pix_fmt yuv420p \
  /home/deploy/tb3_display.mp4 > /home/deploy/ffmpeg_tb3.log 2>&1 || true

wait $APP_PID
