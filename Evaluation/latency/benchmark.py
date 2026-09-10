"""Benchmark real ORT execution, explicitly separating it from game latency."""
import argparse
import json
from pathlib import Path
import platform
import time

import numpy as np
import onnxruntime as ort


def benchmark_model(path, iterations=50, warmup=10, provider="CPUExecutionProvider", threads=2):
    if provider not in ort.get_available_providers():
        raise ValueError(f"Requested provider is unavailable: {provider}; available={ort.get_available_providers()}")
    options = ort.SessionOptions()
    options.intra_op_num_threads, options.inter_op_num_threads = threads, 1
    started = time.perf_counter()
    session = ort.InferenceSession(str(path), sess_options=options, providers=[provider])
    startup = (time.perf_counter()-started)*1000
    shape = session.get_inputs()[0].shape
    if any(not isinstance(dim, int) or dim <= 0 for dim in shape) or shape[0] != 1:
        raise ValueError("Benchmark requires the static batch-one deployment graph")
    data = np.random.default_rng(771).random(shape, dtype=np.float32)
    for _ in range(warmup):
        session.run(None, {"frames": data})
    elapsed = []
    for index in range(iterations):
        data[:, -1, :, 0, 0] = (index % 13)/13
        start = time.perf_counter_ns()
        session.run(None, {"frames": data})
        elapsed.append((time.perf_counter_ns()-start)/1e6)
    return {"measurement": "synchronous ORT session.run on synthetic tensors; includes CPU IO, excludes capture/preprocess/input",
            "platform": platform.platform(), "processor": platform.processor() or "unavailable",
            "onnxruntime": ort.__version__, "requested_provider": provider, "registered_providers": session.get_providers(),
            "provider_partitioning": "not profiled; registered EP list does not prove exclusive node placement",
            "input_shape": shape, "threads": threads, "warmup": warmup, "iterations": iterations,
            "session_creation_ms": startup, "p50_ms": float(np.quantile(elapsed, 0.50)),
            "p95_ms": float(np.quantile(elapsed, 0.95)), "p99_ms": float(np.quantile(elapsed, 0.99)),
            "max_ms": float(max(elapsed)), "raw_ms": elapsed,
            "rtx3070_live_game": False, "game_fps_delta": None, "capture_to_input_ms": None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--iterations", type=int, default=50)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--provider", default="CPUExecutionProvider")
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args()
    if args.iterations < 1 or args.warmup < 0 or args.threads < 1:
        parser.error("Invalid iteration/warmup/thread count")
    result = benchmark_model(args.model, args.iterations, args.warmup, args.provider, args.threads)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2)
        stream.write("\n")
    print(json.dumps({key: value for key, value in result.items() if key != "raw_ms"}, indent=2))


if __name__ == "__main__":
    main()
