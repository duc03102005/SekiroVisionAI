"""Input-dependent ONNX contract fixture for native CI; never gameplay weights."""
import argparse
from pathlib import Path
import numpy as np
import onnx
from onnx import helper, numpy_helper, TensorProto


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    args = parser.parse_args()
    mask = np.zeros((1, 2, 24, 40), np.float32)
    mask[0, 0, 16, 20] = 1; mask[0, 1, 9, 20] = 1
    boxes = np.zeros((1, 2, 4, 24, 40), np.float32)
    boxes[0, 0, :, :, :] = np.array([0.43, 0.54, 0.58, 0.86], np.float32)[:, None, None]
    boxes[0, 1, :, :, :] = np.array([0.34, 0.18, 0.57, 0.49], np.float32)[:, None, None]
    initializers = [numpy_helper.from_array(mask, "mask"), numpy_helper.from_array(boxes, "boxes"),
                    numpy_helper.from_array(np.array(0.8, np.float32), "base"),
                    numpy_helper.from_array(np.array(0.1, np.float32), "scale")]
    nodes = [helper.make_node("ReduceMean", ["frame"], ["mean"], keepdims=0),
             helper.make_node("Mul", ["mean", "scale"], ["change"]),
             helper.make_node("Add", ["change", "base"], ["score"]),
             helper.make_node("Mul", ["score", "mask"], ["scores"])]
    graph = helper.make_graph(nodes, "SYNTHETIC_CONTRACT_FIXTURE_NOT_GAMEPLAY_MODEL",
        [helper.make_tensor_value_info("frame", TensorProto.FLOAT, [1, 3, 192, 320])],
        [helper.make_tensor_value_info("scores", TensorProto.FLOAT, [1, 2, 24, 40]),
         helper.make_tensor_value_info("boxes", TensorProto.FLOAT, [1, 2, 4, 24, 40])], initializer=initializers)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)], ir_version=9)
    values = {"svai.contract": "target-roles-v1", "svai.preprocess": "full-rgb-bilinear-v1",
              "svai.roles": "Wolf,Enemy", "svai.training_status": "synthetic_smoke",
              "svai.semantic_supported": "false", "svai.production_validated": "false",
              "svai.model_version": "SYNTHETIC_TARGET_CONTRACT_FIXTURE", "svai.score_threshold": "0.75"}
    for key, value in values.items():
        field = model.metadata_props.add(); field.key = key; field.value = value
    onnx.checker.check_model(model)
    output = Path(args.output); output.parent.mkdir(parents=True, exist_ok=True); onnx.save(model, output)


if __name__ == "__main__":
    main()
