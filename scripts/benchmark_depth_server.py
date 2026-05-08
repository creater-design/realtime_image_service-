#!/usr/bin/env python3
import argparse
import socket
import statistics
import struct
import time

import cv2
import numpy as np


def recv_exact(conn, size):
    chunks = []
    remaining = size
    while remaining > 0:
        data = conn.recv(remaining)
        if not data:
            raise RuntimeError("connection closed while receiving")
        chunks.append(data)
        remaining -= len(data)
    return b"".join(chunks)


def percentile(values, ratio):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int((len(ordered) - 1) * ratio))
    return ordered[index]


def load_image(path, width, height):
    if path:
        image = cv2.imread(path, cv2.IMREAD_COLOR)
        if image is None or image.size == 0:
            raise RuntimeError(f"failed to read image: {path}")
    else:
        image = np.zeros((height, width, 3), dtype=np.uint8)
        cv2.rectangle(image, (width // 4, height // 3), (width * 3 // 4, height - 20), (80, 180, 220), -1)
        cv2.putText(image, "benchmark", (20, height // 2), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (255, 255, 255), 2)

    if width > 0 and height > 0 and (image.shape[1] != width or image.shape[0] != height):
        image = cv2.resize(image, (width, height), interpolation=cv2.INTER_AREA)

    return image


def main():
    parser = argparse.ArgumentParser(description="Benchmark depth_server TCP inference latency")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--image", default="", help="Optional input image path")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--jpeg_quality", type=int, default=90)
    parser.add_argument("--output", default="/tmp/depth_server_benchmark_depth.jpg")
    args = parser.parse_args()

    image = load_image(args.image, args.width, args.height)
    ok, encoded = cv2.imencode(".jpg", image, [int(cv2.IMWRITE_JPEG_QUALITY), args.jpeg_quality])
    if not ok:
        raise RuntimeError("failed to encode benchmark image")

    request = struct.pack("!I", len(encoded)) + encoded.tobytes()
    latencies_ms = []
    last_payload = b""

    with socket.create_connection((args.host, args.port), timeout=10.0) as conn:
        conn.settimeout(30.0)
        total = args.warmup + args.count
        for index in range(total):
            start = time.perf_counter()
            conn.sendall(request)
            header = recv_exact(conn, 8)
            status, payload_size = struct.unpack("!II", header)
            payload = recv_exact(conn, payload_size)
            elapsed_ms = (time.perf_counter() - start) * 1000.0

            if status != 0:
                raise RuntimeError(payload.decode("utf-8", errors="replace"))

            if index >= args.warmup:
                latencies_ms.append(elapsed_ms)
                last_payload = payload

    if last_payload:
        depth = cv2.imdecode(np.frombuffer(last_payload, dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
        if depth is not None and depth.size > 0:
            cv2.imwrite(args.output, depth)

    avg_ms = statistics.mean(latencies_ms) if latencies_ms else 0.0
    min_ms = min(latencies_ms) if latencies_ms else 0.0
    max_ms = max(latencies_ms) if latencies_ms else 0.0
    p95_ms = percentile(latencies_ms, 0.95)
    fps = 1000.0 / avg_ms if avg_ms > 0.0 else 0.0

    print(f"host: {args.host}")
    print(f"port: {args.port}")
    print(f"image shape: {image.shape}")
    print(f"count: {args.count}")
    print(f"warmup: {args.warmup}")
    print(f"avg_ms: {avg_ms:.2f}")
    print(f"min_ms: {min_ms:.2f}")
    print(f"p95_ms: {p95_ms:.2f}")
    print(f"max_ms: {max_ms:.2f}")
    print(f"fps: {fps:.2f}")
    print(f"output: {args.output}")


if __name__ == "__main__":
    main()
