"""Tiny input-dependent ONNX contract fixture; NEVER a trained gameplay model."""
import argparse
from pathlib import Path
import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--contract", choices=("temporal-v1", "temporal-v2"), default="temporal-v1")
    args = parser.parse_args()
    nodes = [helper.make_node("ReduceMean", ["frames"], ["mean"], keepdims=0),
             helper.make_node("Sigmoid", ["mean"], ["prob"]),
             helper.make_node("Reshape", ["prob", "scalar_shape"], ["attack_probability"]),
             helper.make_node("Identity", ["attack_probability"], ["threat_probability"])]
    initializers = [numpy_helper.from_array(np.array([1, 1], dtype=np.int64), "scalar_shape")]
    outputs = [helper.make_tensor_value_info(name, TensorProto.FLOAT, [1, 1])
               for name in ("attack_probability", "threat_probability")]
    for name, values in (("tti_ms", [100.]), ("tti_uncertainty_ms", [10.]),
                         ("state_logits", [0.] * 9), ("class_logits", [0.] * 14),
                         ("direction_logits", [0.] * 5)):
        initializers.append(numpy_helper.from_array(np.array([values], dtype=np.float32), name))
        outputs.append(helper.make_tensor_value_info(name, TensorProto.FLOAT, [1, len(values)]))
    if args.contract == "temporal-v2":
        # Deliberately nonuniform logits verify an unsupported direction head
        # remains UNKNOWN even when an output tensor has a confident maximum.
        initializers.append(numpy_helper.from_array(
            np.array([[0., 10., 0., 0., 0., 0., 0., 0.]], dtype=np.float32), "attack_direction_logits"))
        outputs.append(helper.make_tensor_value_info("attack_direction_logits", TensorProto.FLOAT, [1, 8]))
    graph = helper.make_graph(nodes, "SVAI_SYNTHETIC_CONTRACT_FIXTURE",
                              [helper.make_tensor_value_info("frames", TensorProto.FLOAT, [1, 16, 3, 320, 320])],
                              outputs, initializer=initializers)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)], ir_version=9)
    helper.set_model_props(model, {"svai.contract": args.contract, "svai.model_version": "SYNTHETIC_CONTRACT_TEST_ONLY",
        "svai.preprocess": "roi-rgb-bilinear-v1", "svai.sample_interval_ms": "33.333333",
        "svai.training_status": "synthetic_smoke", "svai.attack_supported": "false",
        "svai.threat_supported": "false", "svai.tti_supported": "false", "svai.auto_eligible": "false",
        "svai.attack_direction_supported": "false",
        "svai.attack_direction_semantics": "visual_trajectory_screen_with_wolf_reference"})
    onnx.checker.check_model(model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, args.output)
    print(f"Saved synthetic native-runtime fixture (no gameplay training): {args.output}")

if __name__ == "__main__":
    main()
