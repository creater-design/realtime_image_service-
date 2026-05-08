#!/usr/bin/env python3
import argparse
import os
import sys


MODEL_CONFIGS = {
    "vits": {"encoder": "vits", "features": 64, "out_channels": [48, 96, 192, 384]},
    "vitb": {"encoder": "vitb", "features": 128, "out_channels": [96, 192, 384, 768]},
    "vitl": {"encoder": "vitl", "features": 256, "out_channels": [256, 512, 1024, 1024]},
    "vitg": {"encoder": "vitg", "features": 384, "out_channels": [1536, 1536, 1536, 1536]},
}


def require_multiple_of_14(value, name):
    if value <= 0 or value % 14 != 0:
        raise ValueError(f"{name} must be positive and divisible by 14, got {value}")


def main():
    parser = argparse.ArgumentParser(description="Export Depth Anything V2 checkpoint to ONNX")
    parser.add_argument("--repo_path", required=True, help="Path to cloned Depth-Anything-V2 repo")
    parser.add_argument("--checkpoint", required=True, help="Path to depth_anything_v2_<encoder>.pth")
    parser.add_argument("--encoder", choices=sorted(MODEL_CONFIGS.keys()), default="vits")
    parser.add_argument("--output", required=True, help="Output ONNX model path")
    parser.add_argument("--height", type=int, default=280, help="Static input height, divisible by 14")
    parser.add_argument("--width", type=int, default=280, help="Static input width, divisible by 14")
    parser.add_argument("--device", default="cpu", choices=["cpu", "cuda"])
    parser.add_argument("--opset", type=int, default=18)
    parser.add_argument("--verify", action="store_true", help="Run a small ONNX Runtime parity check")
    args = parser.parse_args()

    require_multiple_of_14(args.height, "height")
    require_multiple_of_14(args.width, "width")

    if not os.path.isdir(args.repo_path):
        raise FileNotFoundError(f"Depth Anything V2 repo not found: {args.repo_path}")
    if not os.path.exists(args.checkpoint):
        raise FileNotFoundError(f"checkpoint not found: {args.checkpoint}")

    sys.path.insert(0, args.repo_path)

    import torch
    from depth_anything_v2.dpt import DepthAnythingV2

    if args.device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA export requested but torch.cuda.is_available() is false")

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    model = DepthAnythingV2(**MODEL_CONFIGS[args.encoder])
    state_dict = torch.load(args.checkpoint, map_location="cpu")
    model.load_state_dict(state_dict)
    model = model.to(args.device).eval()

    dummy = torch.zeros(1, 3, args.height, args.width, dtype=torch.float32, device=args.device)

    print(
        f"[export_onnx] exporting encoder={args.encoder} checkpoint={args.checkpoint} "
        f"shape=1x3x{args.height}x{args.width} device={args.device} opset={args.opset}",
        flush=True,
    )

    with torch.no_grad():
        torch.onnx.export(
            model,
            dummy,
            args.output,
            input_names=["image"],
            output_names=["depth"],
            opset_version=args.opset,
            do_constant_folding=True,
        )

    print(f"[export_onnx] wrote {args.output}", flush=True)

    try:
        import onnx

        onnx_model = onnx.load(args.output)
        onnx.checker.check_model(onnx_model)
        print("[export_onnx] onnx.checker passed", flush=True)
    except ImportError:
        print("[export_onnx] onnx package not installed; skip model checker", flush=True)

    if args.verify:
        import numpy as np
        import onnxruntime as ort

        providers = ["CUDAExecutionProvider", "CPUExecutionProvider"] if args.device == "cuda" else ["CPUExecutionProvider"]
        available = set(ort.get_available_providers())
        providers = [provider for provider in providers if provider in available]
        session = ort.InferenceSession(args.output, providers=providers)

        with torch.no_grad():
            torch_output = model(dummy).detach().cpu().numpy()

        ort_output = session.run(["depth"], {"image": dummy.detach().cpu().numpy()})[0]
        max_abs_error = float(np.max(np.abs(torch_output - ort_output)))
        print(
            f"[export_onnx] verify providers={session.get_providers()} "
            f"max_abs_error={max_abs_error:.6f}",
            flush=True,
        )


if __name__ == "__main__":
    main()
