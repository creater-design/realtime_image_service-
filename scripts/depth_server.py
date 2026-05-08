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


class DepthAnythingBackend:
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
            with self.autocast_context():
                raw_depth = self.model.infer_image(image_bgr, self.input_size)

            if self.device == "cuda":
                self.torch.cuda.synchronize()

        return normalize_depth_for_risk(raw_depth, self.invert_depth)


def handle_one_request(conn, backend, response_codec, jpeg_quality):
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
    parser.add_argument("--repo_path", default="", help="Path to cloned Depth-Anything-V2 repo")
    parser.add_argument("--checkpoint", default="", help="Path to depth_anything_v2_<encoder>.pth")
    parser.add_argument("--encoder", choices=sorted(MODEL_CONFIGS.keys()), default="vits")
    parser.add_argument("--input_size", type=int, default=518)
    parser.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda", "mps"])
    parser.add_argument("--precision", default="auto", choices=["auto", "fp32", "fp16", "bf16"])
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--no_invert_depth", action="store_true")
    parser.add_argument("--response_codec", default="jpg", choices=["jpg", "png"])
    parser.add_argument("--jpeg_quality", type=int, default=90)
    args = parser.parse_args()
    args.jpeg_quality = max(1, min(100, args.jpeg_quality))

    backend = DepthAnythingBackend(
        repo_path=args.repo_path,
        checkpoint=args.checkpoint,
        encoder=args.encoder,
        input_size=args.input_size,
        device=args.device,
        invert_depth=not args.no_invert_depth,
        precision=args.precision,
        warmup=args.warmup,
    )

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(16)
        print(
            f"[depth_server] listening on {args.host}:{args.port} "
            f"response_codec={args.response_codec} jpeg_quality={args.jpeg_quality}",
            flush=True,
        )

        while True:
            conn, addr = server.accept()
            thread = threading.Thread(
                target=handle_client,
                args=(conn, addr, backend, args.response_codec, args.jpeg_quality),
                daemon=True,
            )
            thread.start()


if __name__ == "__main__":
    main()
