#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROS_SETUP="/opt/ros/humble/setup.bash"
WORKSPACE_SETUP="${ROOT_DIR}/ros2_ws/install/setup.bash"
DEFAULT_CONFIG_FILE="${ROOT_DIR}/ros2_ws/install/image_publisher_pkg/share/image_publisher_pkg/config/network_camera_obstacle_avoidance.yaml"

if [[ -n "${CONDA_PREFIX:-}" ]]; then
  echo "Detected active Conda environment; removing Conda paths for this ROS2 launch." >&2
  PATH="$(printf '%s' "${PATH}" | tr ':' '\n' | awk '!/miniconda3|anaconda3/' | paste -sd ':' -)"
  unset PYTHONHOME PYTHONPATH CONDA_PREFIX CONDA_DEFAULT_ENV CONDA_PROMPT_MODIFIER CONDA_SHLVL
  export PATH
fi

if [[ ! -f "${WORKSPACE_SETUP}" ]]; then
  echo "ROS2 workspace setup file not found: ${WORKSPACE_SETUP}" >&2
  echo "Please build first:" >&2
  echo "  cd ${ROOT_DIR}/ros2_ws" >&2
  echo "  colcon build --symlink-install" >&2
  exit 1
fi

if [[ ! -f "${ROS_SETUP}" ]]; then
  echo "ROS2 setup file not found: ${ROS_SETUP}" >&2
  exit 1
fi

set +u
source "${ROS_SETUP}"
source "${WORKSPACE_SETUP}"
set -u

STREAM_URL="${STREAM_URL:-}"
CONFIG_FILE="${CONFIG_FILE:-${DEFAULT_CONFIG_FILE}}"
IMAGE_WIDTH="${IMAGE_WIDTH:-}"
IMAGE_HEIGHT="${IMAGE_HEIGHT:-}"
FPS="${FPS:-}"
RECONNECT_INTERVAL="${RECONNECT_INTERVAL:-}"
SERVER_HOST="${SERVER_HOST:-}"
SERVER_PORT="${SERVER_PORT:-}"
MAX_REQUEST_FPS="${MAX_REQUEST_FPS:-}"
OUTPUT_PATH="${OUTPUT_PATH:-}"
DEPTH_OUTPUT_PATH="${DEPTH_OUTPUT_PATH:-}"

if [[ ! -f "${CONFIG_FILE}" ]]; then
  echo "Config file not found: ${CONFIG_FILE}" >&2
  echo "Please build first or set CONFIG_FILE to a valid YAML file:" >&2
  echo "  cd ${ROOT_DIR}/ros2_ws" >&2
  echo "  colcon build --symlink-install" >&2
  exit 1
fi

echo "network camera pipeline config:"
echo "  ROOT_DIR=${ROOT_DIR}"
echo "  CONFIG_FILE=${CONFIG_FILE}"
echo "  STREAM_URL=${STREAM_URL:-<from YAML>}"
echo "  IMAGE_WIDTH=${IMAGE_WIDTH:-<from YAML>}"
echo "  IMAGE_HEIGHT=${IMAGE_HEIGHT:-<from YAML>}"
echo "  FPS=${FPS:-<from YAML>}"
echo "  RECONNECT_INTERVAL=${RECONNECT_INTERVAL:-<from YAML>}"
echo "  SERVER_HOST=${SERVER_HOST:-<from YAML>}"
echo "  SERVER_PORT=${SERVER_PORT:-<from YAML>}"
echo "  MAX_REQUEST_FPS=${MAX_REQUEST_FPS:-<from YAML>}"
echo "  OUTPUT_PATH=${OUTPUT_PATH:-<from YAML>}"
echo "  DEPTH_OUTPUT_PATH=${DEPTH_OUTPUT_PATH:-<from YAML>}"

cmd=(
  ros2 launch image_publisher_pkg network_camera_obstacle_avoidance_launch.py
  config_file:="${CONFIG_FILE}"
)

[[ -n "${STREAM_URL}" ]] && cmd+=(stream_url:="${STREAM_URL}")
[[ -n "${IMAGE_WIDTH}" ]] && cmd+=(image_width:="${IMAGE_WIDTH}")
[[ -n "${IMAGE_HEIGHT}" ]] && cmd+=(image_height:="${IMAGE_HEIGHT}")
[[ -n "${FPS}" ]] && cmd+=(fps:="${FPS}")
[[ -n "${RECONNECT_INTERVAL}" ]] && cmd+=(reconnect_interval:="${RECONNECT_INTERVAL}")
[[ -n "${SERVER_HOST}" ]] && cmd+=(server_host:="${SERVER_HOST}")
[[ -n "${SERVER_PORT}" ]] && cmd+=(server_port:="${SERVER_PORT}")
[[ -n "${MAX_REQUEST_FPS}" ]] && cmd+=(max_request_fps:="${MAX_REQUEST_FPS}")
[[ -n "${OUTPUT_PATH}" ]] && cmd+=(output_path:="${OUTPUT_PATH}")
[[ -n "${DEPTH_OUTPUT_PATH}" ]] && cmd+=(depth_output_path:="${DEPTH_OUTPUT_PATH}")

exec "${cmd[@]}"
