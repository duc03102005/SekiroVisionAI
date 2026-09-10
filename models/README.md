# Versioned models

There are currently **zero trained Sekiro gameplay models** in this repository.
Cataloged video links are not downloaded, rights-cleared, reviewed training data.
No random/synthetic checkpoint is promoted to `attack-v0.1` or `production`.

Create a new directory for every run, for example `models/attack-v0.1-cnn-gru`.
The trainer refuses an existing output directory; ONNX export refuses to replace
an existing `model.onnx`. Each completed export contains `config.json`,
`metrics.json`, `sources.json`, `git_commit.txt`, `checkpoint.pt`, `model.onnx`
and held-out prediction files. Weights/media stay out of Git; publish verified
model bundles as explicitly named release/build artifacts when available.

Synthetic CI artifacts are named `SYNTHETIC-ONLY-temporal-tests-<commit>` and
carry `svai.training_status=synthetic_smoke`, unsupported threat/TTI flags and
`svai.auto_eligible=false`. They verify training/export/runtime plumbing and
cannot arm model Auto Dodge.
