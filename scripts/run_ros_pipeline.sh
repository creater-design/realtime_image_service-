#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

source /opt/ros/humble/setup.bash
source "${ROOT_DIR}/ros2_ws/install/setup.bash"

VIDEO_DEVICE="${VIDEO_DEVICE:-/dev/video0}"
IMAGE_WIDTH="${IMAGE_WIDTH:-160}"
IMAGE_HEIGHT="${IMAGE_HEIGHT:-120}"
FRAMERATE="${FRAMERATE:-30.0}"
PIXEL_FORMAT="${PIXEL_FORMAT:-mjpeg2rgb}"
SERVER_HOST="${SERVER_HOST:-127.0.0.1}"
SERVER_PORT="${SERVER_PORT:-9999}"
MAX_REQUEST_FPS="${MAX_REQUEST_FPS:-2.0}"
OUTPUT_PATH="${OUTPUT_PATH:-/tmp/ros2_camera_result.jpg}"
DEPTH_OUTPUT_PATH="${DEPTH_OUTPUT_PATH:-/tmp/ros2_camera_depth.jpg}"

exec ros2 launch image_publisher_pkg local_camera_obstacle_avoidance_launch.py \
  video_device:="${VIDEO_DEVICE}" \
  image_width:="${IMAGE_WIDTH}" \
  image_height:="${IMAGE_HEIGHT}" \
  framerate:="${FRAMERATE}" \
  pixel_format:="${PIXEL_FORMAT}" \
  server_host:="${SERVER_HOST}" \
  server_port:="${SERVER_PORT}" \
  max_request_fps:="${MAX_REQUEST_FPS}" \
  output_path:="${OUTPUT_PATH}" \
  depth_output_path:="${DEPTH_OUTPUT_PATH}"
