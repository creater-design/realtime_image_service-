#!/usr/bin/env python3
import argparse
import os
import sys

import cv2


def main():
    parser = argparse.ArgumentParser(description="Test an HTTP MJPEG or RTSP camera stream with OpenCV")
    parser.add_argument("--url", default="http://192.168.1.12:8080/video")
    parser.add_argument("--output", default="/tmp/network_camera_test.jpg")
    args = parser.parse_args()

    print(f"url: {args.url}")

    cap = cv2.VideoCapture(args.url)
    opened = cap.isOpened()
    print(f"opened: {opened}")

    if not opened:
        print("error: OpenCV failed to open the stream. Check the URL and network connectivity.", file=sys.stderr)
        return 1

    ret, frame = cap.read()
    print(f"ret: {ret}")

    if not ret or frame is None or frame.size == 0:
        cap.release()
        print("error: stream opened, but failed to read a frame.", file=sys.stderr)
        return 1

    print(f"frame shape: {frame.shape}")

    output_dir = os.path.dirname(args.output)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    if not cv2.imwrite(args.output, frame):
        cap.release()
        print(f"error: failed to write output image: {args.output}", file=sys.stderr)
        return 1

    cap.release()
    print(f"output path: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
