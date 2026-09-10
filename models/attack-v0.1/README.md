# Gameplay model slot

No gameplay-trained model is included in this build. The source catalog contains
reference URLs, but no rights-verified footage with reviewed attack/threat/contact
labels was acquired. Empty directories and synthetic integration fixtures are
not trained Sekiro AI.

Use the existing Training pipeline to create a versioned bundle, then choose its
`model.onnx` with **Load ONNX model** in the Windows app. Bundles include
config.json, metrics.json, sources.json and git_commit.txt. The app verifies the
temporal-v1 input/output/preprocessing metadata and supported heads. An
attack-only model can display live scores while unsupported TTI stays unknown.

Synthetic-only CI exports exercise learning/export/parity but cannot arm model
Auto Dodge. To use the earlier real input path now, explicitly select **CV
heuristic fallback**, return to Sekiro and press F8. It remains experimental and
does not provide a learned attack probability or TTI.
