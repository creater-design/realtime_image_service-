#!/usr/bin/env python3
import argparse
import contextlib
import os
import socket
import struct
import sys
import threading
import traceback

import cv2
import numpy as np


MODEL_CONFIGS = {
    "vits": {"encoder": "vits", "features": 64, "out_channels": [48, 96, 192, 384]},
    "vitb": {"encoder": "vitb", "features": 128, "out_channels": [96, 192, 384, 768]},
    "vitl": {"encoder": "vitl", "features": 256, "out_channels": [256, 512, 1024, 1024]},
    "vitg": {"encoder": "vitg", "features": 384, "out_channels": [1536, 1536, 1536, 1536]},
}


def recv_exact(conn, size, allow_clean_eof=False):
    # TCP does not preserve application message boundaries. Read exactly the number
    # of bytes required by the binary protocol before returning to the caller.
    chunks = []
    remaining = size
    while remaining > 0:
        data = conn.recv(remaining)
        if not data:
            if allow_clean_eof and remaining == size:
                return None
            raise RuntimeError("connection closed while receiving")
        chunks.append(data)
        remaining -= len(data)
    return b"".join(chunks)


def send_response(conn, status, payload):
    # Response header: uint32 status + uint32 payload_size, both network byte order.
    # status=0 means success; status=1 carries a UTF-8 error message.
    conn.sendall(struct.pack("!II", status, len(payload)))
    if payload:
        conn.sendall(payload)


def encode_depth(depth_u8, codec, jpeg_quality):
    if codec == "png":
        ok, encoded = cv2.imencode(".png", depth_u8)
    else:
        ok, encoded = cv2.imencode(
            ".jpg",
            depth_u8,
            [int(cv2.IMWRITE_JPEG_QUALITY), int(jpeg_quality)],
        )

    if not ok:
        raise RuntimeError(f"failed to encode depth {codec}")

    return encoded.tobytes()


def normalize_depth_for_risk(raw_depth, invert_depth):
    depth = raw_depth.astype(np.float32)
    finite_mask = np.isfinite(depth)
    if not finite_mask.any():
        raise RuntimeError("depth output has no finite value")

    valid = depth[finite_mask]
    min_value = float(valid.min())
    max_value = float(valid.max())

    if max_value - min_value < 1e-6:
        norm = np.ones_like(depth, dtype=np.float32)
    else:
        norm = (depth - min_value) / (max_value - min_value)

    norm = np.clip(norm, 0.0, 1.0)

    # Depth Anything V2 relative output is commonly used like inverse depth:
    # larger values tend to mean closer regions. The C++ ROI analyzer expects
    # smaller values to mean closer, so invert by default.
    if invert_depth:
        norm = 1.0 - norm

    return (norm * 255.0).astype(np.uint8)


def preprocess_for_onnx(image_bgr, input_size):
    image_rgb = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2RGB)
    image_rgb = cv2.resize(image_rgb, (input_size, input_size), interpolation=cv2.INTER_CUBIC)
    image = image_rgb.astype(np.float32) / 255.0
    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
    image = (image - mean) / std
    return np.transpose(image, (2, 0, 1))[None, :, :, :].astype(np.float32)


def parse_ort_providers(provider_text):
    providers = [item.strip() for item in provider_text.split(",") if item.strip()]
    return providers or ["CUDAExecutionProvider"]


class PyTorchDepthAnythingBackend:
    def __init__(
        self,
        repo_path,
        checkpoint,
        encoder,
        input_size,
        device,
        invert_depth,
        precision,
        warmup,
    ):
        if repo_path:
            sys.path.insert(0, repo_path)

        import torch
        from depth_anything_v2.dpt import DepthAnythingV2

        if device == "auto":
            if torch.cuda.is_available():
                device = "cuda"
            elif hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
                device = "mps"
            else:
                device = "cpu"

        if not checkpoint:
            checkpoint = os.path.join("checkpoints", f"depth_anything_v2_{encoder}.pth")

        if not os.path.exists(checkpoint):
            raise FileNotFoundError(
                f"checkpoint not found: {checkpoint}. "
                "Download the official Depth Anything V2 checkpoint and pass --checkpoint."
            )

        self.input_size = input_size
        self.device = device
        self.invert_depth = invert_depth
        self.torch = torch
        self.autocast_enabled = False
        self.autocast_dtype = None
        self.lock = threading.Lock()

        if device == "cuda":
            torch.backends.cudnn.benchmark = True
            torch.backends.cuda.matmul.allow_tf32 = True
            torch.backends.cudnn.allow_tf32 = True

            if precision == "auto":
                precision = "fp16"

            if precision == "fp16":
                self.autocast_enabled = True
                self.autocast_dtype = torch.float16
            elif precision == "bf16":
                if hasattr(torch.cuda, "is_bf16_supported") and not torch.cuda.is_bf16_supported():
                    print("[depth_server] bf16 is not supported; falling back to fp16", flush=True)
                    precision = "fp16"
                    self.autocast_dtype = torch.float16
                else:
                    self.autocast_dtype = torch.bfloat16
                self.autocast_enabled = True
        elif precision == "auto":
            precision = "fp32"
        elif precision != "fp32":
            print(
                f"[depth_server] precision={precision} requires CUDA; falling back to fp32",
                flush=True,
            )
            precision = "fp32"

        self.precision = precision

        self.model = DepthAnythingV2(**MODEL_CONFIGS[encoder])
        state_dict = torch.load(checkpoint, map_location="cpu")
        self.model.load_state_dict(state_dict)
        self.model = self.model.to(device).eval()

        print(
            f"[depth_server] loaded Depth Anything V2 encoder={encoder} "
            f"checkpoint={checkpoint} device={device} input_size={input_size} "
            f"precision={precision} autocast={self.autocast_enabled} "
            f"invert_depth={invert_depth}",
            flush=True,
        )

        self.warmup(warmup)

    def autocast_context(self):
        if not self.autocast_enabled:
            return contextlib.nullcontext()

        return self.torch.autocast(
            device_type="cuda",
            dtype=self.autocast_dtype,
        )

    def warmup(self, count):
        if count <= 0:
            return

        dummy = np.zeros((120, 160, 3), dtype=np.uint8)
        for _ in range(count):
            self.infer(dummy)

        if self.device == "cuda":
            self.torch.cuda.synchronize()

        print(f"[depth_server] warmup finished count={count}", flush=True)

    def infer(self, image_bgr):
        with self.lock:
            # Keep model execution serialized. Multiple client threads may exist, but
            # one GPU model instance is easiest to reason about when requests are ordered.
            with self.autocast_context():
                raw_depth = self.model.infer_image(image_bgr, self.input_size)

            if self.device == "cuda":
                self.torch.cuda.synchronize()

        return normalize_depth_for_risk(raw_depth, self.invert_depth)


class OnnxRuntimeDepthBackend:
    def __init__(
        self,
        onnx_path,
        input_size,
        invert_depth,
        providers,
        warmup,
    ):
        try:
            import onnxruntime as ort
        except ImportError as exc:
            raise RuntimeError(
                "onnxruntime is not installed. Install onnxruntime-gpu for CUDA deployment."
            ) from exc

        if not onnx_path:
            raise ValueError("--onnx_path is required when --backend=onnxruntime")
        if not os.path.exists(onnx_path):
            raise FileNotFoundError(f"ONNX model not found: {onnx_path}")

        available = set(ort.get_available_providers())
        selected = [provider for provider in providers if provider in available]
        if not selected:
            raise RuntimeError(
                f"none of requested ONNX Runtime providers are available: {providers}; "
                f"available={sorted(available)}"
            )

        requested_accelerators = [
            provider for provider in selected if provider != "CPUExecutionProvider"
        ]
        if not requested_accelerators:
            raise RuntimeError(
                "GPU provider is required for this project. "
                "Set --ort_providers CUDAExecutionProvider or TensorrtExecutionProvider."
            )

        session_options = ort.SessionOptions()
        session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        self.session = ort.InferenceSession(
            onnx_path,
            sess_options=session_options,
            providers=selected,
        )
        active_providers = self.session.get_providers()
        if requested_accelerators and not any(
            provider in active_providers for provider in requested_accelerators
        ):
            raise RuntimeError(
                "requested accelerator providers are not active: "
                f"requested={requested_accelerators}, active={active_providers}. "
                "For 60 FPS, fix CUDA/cuDNN/onnxruntime-gpu instead of running on CPU."
            )

        self.input_name = self.session.get_inputs()[0].name
        self.output_name = self.session.get_outputs()[0].name
        self.input_size = self._resolve_input_size(input_size)
        self.invert_depth = invert_depth
        self.lock = threading.Lock()

        print(
            f"[depth_server] loaded ONNX Runtime backend onnx_path={onnx_path} "
            f"input_name={self.input_name} output_name={self.output_name} "
            f"input_size={self.input_size} providers={active_providers} "
            f"invert_depth={invert_depth}",
            flush=True,
        )

        self.warmup(warmup)

    def _resolve_input_size(self, requested_size):
        shape = self.session.get_inputs()[0].shape
        if len(shape) == 4 and isinstance(shape[2], int) and isinstance(shape[3], int):
            if shape[2] != shape[3]:
                raise ValueError(f"ONNX model must use square input, got shape={shape}")
            if requested_size != shape[2]:
                print(
                    f"[depth_server] overriding input_size={requested_size} "
                    f"with static ONNX input_size={shape[2]}",
                    flush=True,
                )
            return int(shape[2])
        return requested_size

    def warmup(self, count):
        if count <= 0:
            return

        dummy = np.zeros((120, 160, 3), dtype=np.uint8)
        for _ in range(count):
            self.infer(dummy)

        print(f"[depth_server] ONNX Runtime warmup finished count={count}", flush=True)

    def infer(self, image_bgr):
        original_h, original_w = image_bgr.shape[:2]
        tensor = preprocess_for_onnx(image_bgr, self.input_size)

        with self.lock:
            outputs = self.session.run([self.output_name], {self.input_name: tensor})

        raw_depth = np.squeeze(outputs[0]).astype(np.float32)
        if raw_depth.shape[:2] != (original_h, original_w):
            raw_depth = cv2.resize(raw_depth, (original_w, original_h), interpolation=cv2.INTER_CUBIC)

        return normalize_depth_for_risk(raw_depth, self.invert_depth)


def create_backend(args):
    if args.backend == "pytorch":
        return PyTorchDepthAnythingBackend(
            repo_path=args.repo_path,
            checkpoint=args.checkpoint,
            encoder=args.encoder,
            input_size=args.input_size,
            device=args.device,
            invert_depth=not args.no_invert_depth,
            precision=args.precision,
            warmup=args.warmup,
        )

    return OnnxRuntimeDepthBackend(
        onnx_path=args.onnx_path,
        input_size=args.input_size,
        invert_depth=not args.no_invert_depth,
        providers=parse_ort_providers(args.ort_providers),
        warmup=args.warmup,
    )


def handle_one_request(conn, backend, response_codec, jpeg_quality):
    # Request format must match DepthEstimator in C++:
    # uint32 image_size followed by image_size JPEG bytes.
    size_bytes = recv_exact(conn, 4, allow_clean_eof=True)
    if size_bytes is None:
        return False

    (image_size,) = struct.unpack("!I", size_bytes)
    if image_size == 0:
        raise RuntimeError("empty image payload")

    image_bytes = recv_exact(conn, image_size)
    image_array = np.frombuffer(image_bytes, dtype=np.uint8)
    image_bgr = cv2.imdecode(image_array, cv2.IMREAD_COLOR)
    if image_bgr is None or image_bgr.size == 0:
        raise RuntimeError("failed to decode input jpeg")

    depth_u8 = backend.infer(image_bgr)
    send_response(conn, 0, encode_depth(depth_u8, response_codec, jpeg_quality))
    return True


def handle_client(conn, addr, backend, response_codec, jpeg_quality):
    request_count = 0
    with conn:
        try:
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            pass
        print(f"[depth_server] client connected: {addr}", flush=True)

        while True:
            try:
                # A single TCP connection can carry many frames. This avoids per-frame
                # connect/close overhead from the C++ image_server.
                if not handle_one_request(conn, backend, response_codec, jpeg_quality):
                    break
                request_count += 1
            except Exception as exc:
                message = f"{type(exc).__name__}: {exc}"
                print(f"[depth_server] request from {addr} failed: {message}", flush=True)
                traceback.print_exc()
                try:
                    send_response(conn, 1, message.encode("utf-8"))
                except Exception:
                    break

        print(
            f"[depth_server] client closed: {addr} requests={request_count}",
            flush=True,
        )


def main():
    parser = argparse.ArgumentParser(description="Depth Anything V2 TCP inference server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--backend", default="pytorch", choices=["pytorch", "onnxruntime"])
    parser.add_argument("--repo_path", default="", help="Path to cloned Depth-Anything-V2 repo")
    parser.add_argument("--checkpoint", default="", help="Path to depth_anything_v2_<encoder>.pth")
    parser.add_argument("--encoder", choices=sorted(MODEL_CONFIGS.keys()), default="vits")
    parser.add_argument("--input_size", type=int, default=518)
    parser.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda", "mps"])
    parser.add_argument("--precision", default="auto", choices=["auto", "fp32", "fp16", "bf16"])
    parser.add_argument("--onnx_path", default="", help="Path to exported Depth Anything V2 ONNX model")
    parser.add_argument(
        "--ort_providers",
        default="CUDAExecutionProvider",
        help="Comma-separated ONNX Runtime GPU providers, e.g. CUDAExecutionProvider",
    )
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--no_invert_depth", action="store_true")
    parser.add_argument("--response_codec", default="jpg", choices=["jpg", "png"])
    parser.add_argument("--jpeg_quality", type=int, default=90)
    args = parser.parse_args()
    args.jpeg_quality = max(1, min(100, args.jpeg_quality))

    backend = create_backend(args)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(16)
        print(
            f"[depth_server] listening on {args.host}:{args.port} backend={args.backend} "
            f"response_codec={args.response_codec} jpeg_quality={args.jpeg_quality}",
            flush=True,
        )

        while True:
            conn, addr = server.accept()
            # Each connection gets a lightweight handler thread. Inference itself is
            # serialized by DepthAnythingBackend.lock to protect the model/GPU state.
            thread = threading.Thread(
                target=handle_client,
                args=(conn, addr, backend, args.response_codec, args.jpeg_quality),
                daemon=True,
            )
            thread.start()


if __name__ == "__main__":
    main()
