# Temporal training pipeline

This is executable offline tooling inside SekiroVisionAI. It does not add Python
to the Windows app. It implements three causal baselines, masked multi-task
learning, source-group splits, early ONNX export, calibration, review queues and
CPU/provider latency measurement. Capture and native model integration proceed
independently; no milestone PASS/FAIL gate is involved.

**Current evidence:** no permission-cleared Sekiro videos or reviewed gameplay
labels were acquired in this session. The public source catalog is research,
not a dataset already trained. The CI experiment optimizes generated colored
tiles and checks all three exported networks. Those artifacts are explicitly
synthetic, cannot enable Auto Dodge, and provide no boss accuracy or live latency
claim. See `Dataset/catalog/source_research.md` for concrete acquisition results.
The completed local run is recorded in
`Evaluation/reports/synthetic-cpu-2026-09-10.json`, including the exact training
code hash and dirty-worktree flag. All three optimizers reduced their synthetic
loss and all three ONNX exports passed numerical parity. Shared-host CPU timing
varied between runs; it is not a winner selection or a game latency claim.

## Install

Python3.12 and FFmpeg/ffprobe are required for offline video tools. On Windows:

```powershell
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install --index-url https://download.pytorch.org/whl/cpu torch==2.8.0
.venv\Scripts\python -m pip install -r requirements-training.txt
```

For CUDA training, use PyTorch's supported2.8 CUDA wheel index instead of the CPU
index and pass `--device cuda`. Verify the driver/package combination before
claiming RTX3070 support; the CPU CI runner cannot measure it. ONNX Runtime here
is pinned1.22.1 for Python parity; the native app's separately pinned runtime may
be newer and has its own load/run test. Export uses standard ONNX opset17.

On Linux replace `.venv\Scripts\python` with `.venv/bin/python`. The isolated
training environment was verified after dependency installation. Local synthetic
tests and ordinary repository CI execute the same tensor/export checks; their
generated evidence records the actual host and package versions.

## Data and supervised training

1. Acquire media through the permitted downloader or register authorized local
   media. Mine clips and review suggestions in `DatasetTools/AnnotationApp`.
2. Export reviewed annotations with `DatasetTools.annotation.export_samples`.
   See its `--help` for current paths/arguments. Its JSONL exchange preserves
   source IDs, clip-local bounds, original source timestamps and label masks.
3. Freeze groups, then train one small baseline immediately; no complete boss
   dataset is required. Do not fabricate contact times for avoided attacks.

Run these commands at repository root (use the venv Python):

```bash
python -m DatasetTools.validation.splits --sources local-data/source_manifest.jsonl --output local-data/splits-v1 --held-out-boss "Lady Butterfly"
python -m Training.trainers.train local-data/samples.jsonl local-data/splits-v1 models/attack-v0.1-cnn-gru --model-version attack-v0.1-cnn-gru --architecture cnn_gru --epochs 5
python -m Training.export.onnx_bundle models/attack-v0.1-cnn-gru
python -m Evaluation.latency.benchmark models/attack-v0.1-cnn-gru/model.onnx models/attack-v0.1-cnn-gru/cpu-latency.json
```

Run the identical frozen split with `--architecture feature_tcn` and
`--architecture video_transformer`, each in a new version directory. Config
reference files in `Training/configs` describe the defaults; the CLI records the
actual used arguments in the output `config.json`. Change `--frames` among
8/16/24/32/48 and `--size` among320/384/416/512 for deliberate experiments.
Changing temporal cadence/length requires a corresponding trained/exported
configuration; resizing a checkpoint does not prove equivalent quality.

To execute all three training/export/latency runs and write one comparison:

```bash
python -m Training.trainers.compare_baselines local-data/samples.jsonl local-data/splits-v1 models/comparison-v0.1 --version-prefix attack-v0.1 --epochs 5
```

This command runs real training on the supplied reviewed data. It refuses
synthetic examples through the supervised trainer and does not silently replace
missing media with random tensors. It writes `comparison.json` after each model
so completed runs remain reviewable if a later architecture fails.

The CNN+GRU and transformer use a small depthwise CNN frame encoder. The TCN
baseline uses RGB spatial appearance cells and backward frame differences as an
explicit **feature proxy**, not detected objects, anatomical pose, optical flow,
or a tracked weapon. It makes baseline comparison possible before a licensed,
Sekiro-compatible detector/pose model is available. The transformer has two
small causal attention layers. None receives future frames.

The canonical DatasetTools splitter uses acquired source provenance, including
creator/URL/hash duplicate information. The trainer understands its combined
test list and separate held-out-boss subset and reports the two tests separately.
`Training.datasets.splits` is an additional sample-manifest-only helper when a
canonical source split is unavailable; it cannot recover missing creator/hash
metadata and reports only the recorded grouping evidence.

Training requires `annotation_status=reviewed` plus reviewer identity and rejects
`example_only` media. Missing labels have zero loss masks. Point TTI is trained
only for uncensored `OBSERVED_CONTACT`; no-hit and unobserved contact remain
masked. `threat` is an independently reviewed intersection/threat label, not an
automatic copy of `attack`. Direction is observed player movement in screen
coordinates, not a demonstrated safe input. A no-hit dataset can train attack
heads while truthfully leaving threat/TTI unsupported.

Every run records source/split hashes, seed, optimizer progress, supported-label
counts, per-boss/per-class clip-anchor metrics and held-out predictions. Sigmoid
scores have no calibration claim when validation lacks both classes. Temperature
and thresholds are fitted only on validation data; missing validation produces
an explicit status and configurable conservative defaults, not a development
gate. Test and held-out-boss evaluation happen once after those settings freeze.

## Native ONNX contract

Input `frames` is float32 `[1,T,3,H,W]`, RGB/255, causal oldest-to-newest history
at33.333333ms per sample. DefaultT16/H320 spans500ms from first to last sample.
Source crop uses normalized ROI bounds: floor(left×W), floor(top×H),
ceil(right×W), ceil(bottom×H), followed by bilinear half-pixel square resize,
`align_corners=false`, no letterbox. Default ROI is[.28,.12,.72,.61]. The native
runtime resets on stream/ROI/timestamp discontinuities and warms a full history.

Outputs, in order:

| Name | Shape | Meaning |
|---|---|---|
| attack_probability | [1,1] | Attack-event score; calibrated only when recorded validation supports it |
| threat_probability | [1,1] | Separately supervised threat score, not simply attack probability |
| tti_ms | [1,1] | Nonnegative Laplace location for observed-contact timing |
| tti_uncertainty_ms | [1,1] | Laplace scale; not a calibrated coverage guarantee |
| state_logits | [1,9] | IDLE/WALK/RUN/TURN/WINDUP/ACTIVE/RECOVERY/COMBO/FEINT |
| class_logits | [1,14] | Exact order in Training.datasets.video_samples.CLASSES |
| direction_logits | [1,5] | Observed LEFT/RIGHT/FORWARD/BACK/NEUTRAL, not recommended keys |

Metadata contains `svai.contract=temporal-v1`, model version, training status,
preprocess/cadence and trained-head support flags. A supervised attack-only model
can load for live scores while TTI/threat remain unavailable. Synthetic,
untrained and self-supervised-only models never become model Auto Dodge simply
because ONNX can execute. The threat decision engine still owns action timing,
hysteresis, episode identity, cooldown and input guards.

Export checks ONNX structure and executes three actual CPU ORT numerical-parity
cases against PyTorch with rtol/atol1e-4. A passed parity test validates export,
not game recognition. ORT timing reports warmed p50/p95/p99/max and raw samples,
startup cost, tensor shape, provider list and host. It excludes capture,
preprocessing and input, and explicitly does not claim RTX3070/game performance.

## Review and retrain

After each supervised run, `hard_negative_review_queue.jsonl` prioritizes high
threat on reviewed negatives, unreviewed high-score candidates and uncertainty.
It never writes accepted labels. Held-out test data is not recycled into train
without retiring that holdout and making a new independent test.

```bash
python -m Evaluation.offline.predict models/attack-v0.1-cnn-gru/model.onnx local-data/unlabeled-windows.jsonl local-data/model-v1-predictions.jsonl --review-queue local-data/review-v2.jsonl
python -m Evaluation.false_positive.mine local-data/model-v1-predictions.jsonl local-data/hard-negatives-v2.jsonl
```

Review these clips, export a revised manifest, then create `attack-v0.2-*` with
the same training command. Live sample recording in the app supplies additional
authorized source sessions; visual Dodge onset is not a known input timestamp
unless the recording includes the app's own input log.

`Evaluation.offline.events` matches emitted threat episodes to reviewed source
event intervals one-to-one. Repeated detections count as false detections. It
reports lead time only with observed contact, and false Dodge metrics only with
reviewed sent decisions and measured non-threat exposure. Clip-anchor precision
alone is not event precision or false Dodge per minute.

## Optional self-supervision and smoke tests

```bash
python -m Training.trainers.pretrain local-data/unlabeled-windows.jsonl models/representation-v0.1 --architecture cnn_gru
python -m Training.trainers.train local-data/samples.jsonl local-data/splits-v1 models/attack-v0.2-cnn-gru --model-version attack-v0.2-cnn-gru --pretrained models/representation-v0.1/checkpoint.pt
python -m unittest discover -s Tests -p 'training_*.py'
python -m Training.trainers.smoke artifacts/temporal-synthetic --steps 80
```

The optional pretrainer implements symmetric contrastive clip embeddings with
two photometric views. It is deliberately small, does not claim VideoMAE or
pose learning, and leaves every decision head unsupported until fine-tuning.
Use only training-source unlabeled media for pretraining if a held-out source/
boss comparison is intended; pretraining exposure must be disclosed.

Synthetic smoke optimization trains all three architectures on mathematical
colored-tile sequences at64×64, then executes parity and actual CPU latency at
the declared deployment shape (default16×320×320). The changed image size is
explicit in evidence; this is an implementation test, not task generalization.

## Primary API/research references

- [PyTorch2.8 ONNX exporters](https://docs.pytorch.org/docs/2.8/onnx.html): explicit static TorchScript exporter selection preserves standard GRU export for this baseline; no unsupported dynamic shape promise.
- [ONNX Runtime Python API](https://onnxruntime.ai/docs/api/python/api_summary.html): explicit execution providers and synchronous session.run measurement.
- [TCN reference paper](https://arxiv.org/abs/1803.01271): architectural motivation, not a Sekiro performance claim.
- [VideoMAE implementation](https://github.com/MCG-NJU/VideoMAE): heavier future pretraining candidate, not a dependency or claimed implementation here.

Context7 was used to verify the PyTorch2.8 exporter/API path. Benchmark claims
must come from the generated run artifact, not those papers or documentation.
