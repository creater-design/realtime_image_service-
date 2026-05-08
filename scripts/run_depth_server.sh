#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PYTHON_BIN="${PYTHON_BIN:-python3}"
DEPTH_BACKEND="${DEPTH_BACKEND:-onnxruntime}"
DEPTH_REPO="${DEPTH_REPO:-${ROOT_DIR}/Depth-Anything-V2-main}"
DEPTH_CHECKPOINT="${DEPTH_CHECKPOINT:-${DEPTH_REPO}/checkpoints/depth_anything_v2_vits.pth}"
DEPTH_ENCODER="${DEPTH_ENCODER:-vits}"
DEPTH_INPUT_SIZE="${DEPTH_INPUT_SIZE:-280}"
DEPTH_DEVICE="${DEPTH_DEVICE:-cuda}"
DEPTH_PRECISION="${DEPTH_PRECISION:-auto}"
DEPTH_ONNX_PATH="${DEPTH_ONNX_PATH:-${ROOT_DIR}/models/depth_anything_v2_${DEPTH_ENCODER}_${DEPTH_INPUT_SIZE}.onnx}"
DEPTH_ORT_PROVIDERS="${DEPTH_ORT_PROVIDERS:-CUDAExecutionProvider}"
DEPTH_WARMUP="${DEPTH_WARMUP:-2}"
DEPTH_HOST="${DEPTH_HOST:-127.0.0.1}"
DEPTH_PORT="${DEPTH_PORT:-18080}"
DEPTH_RESPONSE_CODEC="${DEPTH_RESPONSE_CODEC:-jpg}"
DEPTH_JPEG_QUALITY="${DEPTH_JPEG_QUALITY:-90}"

if [[ -n "${CONDA_PREFIX:-}" && -d "${CONDA_PREFIX}/lib" ]]; then
  export LD_LIBRARY_PATH="${CONDA_PREFIX}/lib:${LD_LIBRARY_PATH:-}"
fi

if [[ "${DEPTH_BACKEND}" != "pytorch" && "${DEPTH_BACKEND}" != "onnxruntime" ]]; then
  echo "Invalid DEPTH_BACKEND=${DEPTH_BACKEND}; expected pytorch or onnxruntime" >&2
  exit 1
fi

if [[ "${DEPTH_BACKEND}" == "pytorch" ]]; then
  if [[ ! -d "${DEPTH_REPO}" ]]; then
    echo "Depth Anything V2 repo not found: ${DEPTH_REPO}" >&2
    exit 1
  fi

  if [[ ! -f "${DEPTH_CHECKPOINT}" ]]; then
    echo "Depth Anything V2 checkpoint not found: ${DEPTH_CHECKPOINT}" >&2
    exit 1
  fi
else
  if [[ ! -f "${DEPTH_ONNX_PATH}" ]]; then
    echo "ONNX model not found: ${DEPTH_ONNX_PATH}" >&2
    echo "Export it first with scripts/export_depth_anything_onnx.py" >&2
    exit 1
  fi
fi

echo "depth server config:"
echo "  ROOT_DIR=${ROOT_DIR}"
echo "  CONDA_PREFIX=${CONDA_PREFIX:-<none>}"
echo "  DEPTH_BACKEND=${DEPTH_BACKEND}"
echo "  DEPTH_REPO=${DEPTH_REPO}"
echo "  DEPTH_CHECKPOINT=${DEPTH_CHECKPOINT}"
echo "  DEPTH_ENCODER=${DEPTH_ENCODER}"
echo "  DEPTH_INPUT_SIZE=${DEPTH_INPUT_SIZE}"
echo "  DEPTH_DEVICE=${DEPTH_DEVICE}"
echo "  DEPTH_PRECISION=${DEPTH_PRECISION}"
echo "  DEPTH_ONNX_PATH=${DEPTH_ONNX_PATH}"
echo "  DEPTH_ORT_PROVIDERS=${DEPTH_ORT_PROVIDERS}"
echo "  DEPTH_WARMUP=${DEPTH_WARMUP}"
echo "  DEPTH_HOST=${DEPTH_HOST}"
echo "  DEPTH_PORT=${DEPTH_PORT}"
echo "  DEPTH_RESPONSE_CODEC=${DEPTH_RESPONSE_CODEC}"
echo "  DEPTH_JPEG_QUALITY=${DEPTH_JPEG_QUALITY}"

exec "${PYTHON_BIN}" "${ROOT_DIR}/scripts/depth_server.py" \
  --host "${DEPTH_HOST}" \
  --port "${DEPTH_PORT}" \
  --backend "${DEPTH_BACKEND}" \
  --repo_path "${DEPTH_REPO}" \
  --checkpoint "${DEPTH_CHECKPOINT}" \
  --encoder "${DEPTH_ENCODER}" \
  --input_size "${DEPTH_INPUT_SIZE}" \
  --device "${DEPTH_DEVICE}" \
  --precision "${DEPTH_PRECISION}" \
  --onnx_path "${DEPTH_ONNX_PATH}" \
  --ort_providers "${DEPTH_ORT_PROVIDERS}" \
  --warmup "${DEPTH_WARMUP}" \
  --response_codec "${DEPTH_RESPONSE_CODEC}" \
  --jpeg_quality "${DEPTH_JPEG_QUALITY}"
