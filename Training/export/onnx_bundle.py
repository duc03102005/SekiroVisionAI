"""Export a fixed-shape batch-one causal graph and verify CPU ORT parity."""
import argparse
import json
from pathlib import Path
import time

import numpy as np
import onnx
import onnxruntime as ort
import torch

from Training.models import build_model, OUTPUT_NAMES
from Training.models.temporal import OUTPUT_NAMES_V1
from Training.trainers.train import sha256, write_json


def export_bundle(bundle, parity_cases=3):
    bundle = Path(bundle)
    target = bundle/"model.onnx"
    if target.exists():
        raise FileExistsError(f"Versioned model already exists: {target}")
    checkpoint = torch.load(bundle/"checkpoint.pt", map_location="cpu", weights_only=True)
    config = checkpoint["config"]
    status = config.get("training_status", "untrained")
    if status not in ("trained", "synthetic_smoke", "untrained", "self_supervised"):
        raise ValueError(f"Unknown training status: {status}")
    trained = status == "trained" and config.get("trained_samples", 0) > 0 and config.get("supervised_epochs", 0) > 0
    if status == "trained" and not trained:
        raise ValueError("Refusing a 'trained' model with no supervised learning evidence")
    contract = config.get("contract", "temporal-v1")
    output_names = OUTPUT_NAMES if contract == "temporal-v2" else OUTPUT_NAMES_V1
    model = build_model(config["architecture"], config["frames"], config["width"], contract=contract).eval()
    model.load_state_dict(checkpoint["model"])
    shape = (1, config["frames"], 3, config["size"], config["size"])
    generator = torch.Generator().manual_seed(914)
    example = torch.rand(shape, generator=generator)
    temporary = bundle/"model.pending.onnx"
    started = time.perf_counter()
    # Static B/T/H/W and no data-dependent control flow: the explicit legacy
    # exporter preserves GRU as a standard ONNX GRU node in PyTorch2.8.
    torch.onnx.export(model, (example,), str(temporary), input_names=["frames"],
                      output_names=output_names, opset_version=17, dynamo=False,
                      do_constant_folding=True, export_params=True)
    graph = onnx.load(temporary)
    metadata = {
        "svai.contract": contract, "svai.model_version": str(config["model_version"]),
        "svai.training_status": status, "svai.sample_interval_ms": "33.333333",
        "svai.preprocess": "roi-rgb-bilinear-v1",
        "svai.tti_supported": str(trained and bool(config.get("tti_supported"))).lower(),
        "svai.threat_supported": str(trained and bool(config.get("threat_supported"))).lower(),
        "svai.attack_supported": str(trained and bool(config.get("attack_supported"))).lower(),
        "svai.attack_direction_supported": str(trained and contract == "temporal-v2" and bool(config.get("attack_direction_supported"))).lower(),
        "svai.state_supported": str(trained and bool(config.get("state_supported"))).lower(),
        "svai.class_supported": str(trained and bool(config.get("class_supported"))).lower(),
        "svai.observed_direction_supported": str(trained and bool(config.get("observed_direction_supported"))).lower(),
        "svai.auto_eligible": str(trained and bool(config.get("auto_eligible"))).lower(),
        "svai.direction_semantics": "observed_screen_action_not_recommended_input",
        "svai.attack_direction_semantics": "visual_trajectory_screen_with_wolf_reference",
        "svai.attack_direction_confidence_semantics": "uncalibrated_softmax_maximum_not_safe_action_probability",
        "svai.uncertainty_semantics": "laplace_scale_ms_not_calibrated_coverage",
    }
    onnx.helper.set_model_props(graph, metadata)
    onnx.checker.check_model(graph, full_check=True)
    onnx.save(graph, temporary)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(str(temporary), sess_options=options, providers=["CPUExecutionProvider"])
    if [output.name for output in session.get_outputs()] != output_names:
        raise RuntimeError("Export output names/order mismatch")
    maximum_errors = {name: 0.0 for name in output_names}
    for case in range(parity_cases):
        frames = torch.rand(shape, generator=generator) if case else torch.zeros(shape)
        if case == 2:
            frames[:, :config["frames"]//2] *= 0.05
        with torch.no_grad():
            expected = model(frames)
        actual = session.run(output_names, {"frames": frames.numpy()})
        for name, reference, exported in zip(output_names, expected, actual):
            wanted = reference.numpy()
            if not np.isfinite(exported).all():
                raise RuntimeError(f"Non-finite ONNX output: {name}")
            np.testing.assert_allclose(exported, wanted, rtol=1e-4, atol=1e-4, err_msg=name)
            maximum_errors[name] = max(maximum_errors[name], float(np.max(np.abs(wanted-exported))))
    temporary.replace(target)
    parity = {"status": "passed", "cases": parity_cases, "input_shape": shape,
              "providers": session.get_providers(), "onnxruntime": ort.__version__, "onnx": onnx.__version__,
              "opset": 17, "rtol": 1e-4, "atol": 1e-4, "max_absolute_error": maximum_errors,
              "export_and_parity_seconds": time.perf_counter()-started,
              "input_evidence": "synthetic tensors validate numerical parity, not gameplay accuracy"}
    config.update({"model_sha256": sha256(target), "onnx_opset": 17, "onnx_metadata": metadata})
    write_json(bundle/"config.json", config)
    metrics_path = bundle/"metrics.json"
    metrics = json.loads(metrics_path.read_text(encoding="utf-8")) if metrics_path.exists() else {}
    metrics["onnx_parity"] = parity
    write_json(metrics_path, metrics)
    print(json.dumps({"model": str(target), "training_status": status, "parity": parity}), flush=True)
    return parity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    export_bundle(args.bundle)


if __name__ == "__main__":
    main()
