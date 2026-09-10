# Dataset tools: media → reviewable clips → temporal labels → training

These offline Python tools run separately from the native Windows app. They do
not require a finished capture benchmark or a complete dataset. The source
catalog in `Dataset/catalog/video_sources.csv` is a list of references; it is
**not downloaded or licensed training media**. Current source availability and
permission evidence are recorded in `Dataset/catalog/source_research.md`.

## Install

Use Python 3.12, FFmpeg/ffprobe on PATH, and the pinned Python dependencies:

```powershell
py -3.12 -m venv .venv-dataset
.venv-dataset\Scripts\Activate.ps1
python -m pip install -r DatasetTools/requirements.txt
ffmpeg -version
ffprobe -version
```

The annotation app uses Python's Tk desktop toolkit plus Pillow. Official
Windows Python installers include Tcl/Tk; on Linux install the distribution's
Tk package if it is absent. No browser server, cloud upload or Python process is
required by `SekiroAutoDodge.exe`.

## 1. Import a permitted video

Record an actual ownership, permission or open-license basis. The importer
stores an immutable media SHA256, source/session/player IDs, provenance, decoder
version, codec/resolution and **actual per-frame ffprobe PTS**. A nominal FPS is
never substituted for missing timestamps. Media and machine-local manifests go
under ignored `data/`; source URLs and reviewed annotations can be versioned
separately after removing machine-local paths.

```powershell
python -m DatasetTools.downloader.ingest --input C:/recordings/fight.mp4 --source-id LOCAL_001 --creator my-player-id --session-id session_001 --boss "Genichiro Ashina" --usage-basis OWN_RECORDING --usage-evidence "My locally recorded gameplay; permitted project analysis" --root data
```

`--input` also accepts a direct HTTPS video URL when the supplied usage evidence
permits that acquisition. `--usage-basis OPEN_LICENSE` or `EXPLICIT_PERMISSION`
must describe the real license/permission using `--usage-evidence`,
`--license-note` and, where appropriate, `--source-url`. The command does not
verify a license on your behalf. YouTube/Twitch/Bilibili/Reddit/Steam watch pages
are reference-only; there is no platform-stream extractor or access bypass.
Use an authorized creator export or other permitted acquisition route.

## 2. Automatically propose and extract clips

```powershell
python -m DatasetTools.clip_miner.mine --sources data/source_manifest.jsonl --source-id LOCAL_001 --output data/clips --manifest data/clips.jsonl --clip-seconds 3 --max-clips 120
```

The miner samples long videos at 5 Hz for analysis, estimates background camera
translation/rotation/scale with sparse Lucas–Kanade tracks and robust affine
fitting, then subtracts camera flow from dense local optical flow. Local motion,
acceleration and residual frame difference propose attack-like windows. Idle,
camera-only motion and scene cuts also yield review candidates. This is motion
mining, not a trained boss/weapon detector. All proposed classes and threat labels
remain unreviewed; a camera-motion candidate is not automatically a negative.

Output clips are 2–4 seconds (3 by default), H264 CRF18, 30 FPS, up to 1280 pixels
wide. `--max-width 0` retains source dimensions. `--roi left top right bottom`
changes the normalized combat proposal region. A `.frames.jsonl` sidecar maps
each output frame to the latest original observation at or before that requested
time, with original source-frame index/PTS, requested PTS and duplicate flags.
Resampling never invents intermediate movement. Extraction decodes the source
once for all selected clips; it does not dump every frame into the dataset.

Each clip has an immutable ID, media hash, provenance and a `.json` sidecar.
The aggregate clip manifest is append-only. Use a fresh output directory for a
new extraction version; do not run concurrent writers against one manifest.

## 3. Import native app recordings directly

Enable **Record Training Samples** in the Windows app. It writes the JPEG frame
bundles described in `docs/recording-format.md`. Import one completed sample:

```powershell
python -m DatasetTools.downloader.import_recording --bundle "C:/Users/me/AppData/Local/SekiroVisionAI/recordings/session-ID/sample-000001" --creator my-player-id --usage-evidence "My local SekiroVisionAI recording" --boss "Genichiro Ashina" --root data
```

Use the exact sample directory shown by the app/log, since session IDs differ.
The importer preserves the original JPEGs, `sample.json`, capture sequence IDs
and exact QPC timestamps. It generates a safe concat from validated frame names,
creates a lossless decoded review source and an unreviewed 30-FPS clip, with
duplicated observations explicitly marked. Encoded video timestamps do not
replace the original QPC timing. Truncated history remains truncated. Samples
from one app session are grouped together for splitting. `DODGE_SENT`,
`FALSE_POSITIVE` and `MISSED_ATTACK` markers are review requests, not labels.

## 4. Review temporal labels locally

```powershell
python -m DatasetTools.AnnotationApp --clips data/clips.jsonl --annotations data/annotations.jsonl --reviewer my-reviewer-id
```

The app provides play/pause, frame stepping, quarter/half speed, original PTS,
ROI dragging, state/class/contact/direction fields, flow-based boundary
suggestions, copy of semantic labels and multiple strike IDs within a combo.

| Key | Action |
|---|---|
| Space | Play/pause |
| Left / Right | Previous/next decoded clip frame |
| W / A | Mark windup / active start |
| I | Mark visibly observed contact |
| R / E | Mark recovery start / end |
| D | Mark observed player Dodge animation onset |
| Ctrl+S | Accept the current annotation as reviewed |

Shortcuts do not consume typing inside text fields. **Save proposal** preserves
unreviewed work; **Accept reviewed** records reviewer, timestamp and an appended
revision. Save work before changing clips or closing. Copied labels and motion
suggestions require review; contact times and accepted status are never copied.

For a whole-clip negative, watch the whole clip, choose its negative reason and
use **Mark whole clip NON_THREAT**, then accept. Before-windup context is not
automatically labeled negative. Attack timeline boundaries expand only over
reviewed windup/active/recovery intervals. Event-level threat labels do not
propagate into recovery. Arbitrary class predictions are never trusted as labels.

`Dataset/annotation.schema.json` plus
`DatasetTools/annotation/contract.py` define schema and cross-field validation.
All boundaries use absolute original decoded source-frame indices; unknown
values are null. `OBSERVED_CONTACT` requires visible contact inside the active
interval. No-hit/missed/occluded/censored contact has no exact point TTI. Reviewed
counterfactual intervals are stored separately and masked out of point-TTI loss.
Observed Dodge direction is a player observation, **not a proven safe action**.

## 5. Freeze leakage-free sources and export causal samples

```powershell
python -m DatasetTools.validation.splits --sources data/source_manifest.jsonl --output data/splits/v001 --held-out-boss "Guardian Ape"
python -m DatasetTools.annotation.export_samples --clips data/clips.jsonl --annotations data/annotations.jsonl --output data/samples-v001.jsonl --sequence-length 16 --stride 3
python -m DatasetTools.validation.validate --sources data/source_manifest.jsonl --clips data/clips.jsonl --annotations data/annotations.jsonl --splits data/splits/v001/split_manifest.json --verify-hashes
```

The splitter unions full source, creator/player, session, duplicate group, hash
and URL before assigning train/validation/test. Held-out-boss sources and linked
groups go entirely to test. Normal unseen-source and held-out-boss test IDs are
separate subsets. Too few independent groups yields an explicit missing
validation/test warning, not a generalization claim. Use only validation to tune
thresholds. Never randomly split frames from a full source.

The exporter emits the interface consumed by `Training/`: clip-local half-open
`start_frame/end_frame`, original final `source_pts_ms`, RGB ROI, source grouping
metadata and nullable multi-task labels. History ends at the sample anchor and
does not look into the future. Future observed impact supplies a training
target only; it is absent from model inputs. Overlapping conflicting reviewed
strike labels are rejected for correction. Synthetic fixtures are excluded by
default and retain `example_only=true` even with the explicit test-only flag.

## 6. Model review / hard negatives

After model inference produces JSONL with `clip_id`, `model_version` and
`threat_probability`, build a review queue:

```powershell
python -m DatasetTools.annotation.review_queue --predictions data/predictions-v001.jsonl --clips data/clips.jsonl --annotations data/annotations.jsonl --output data/review-v002.jsonl
python -m DatasetTools.AnnotationApp --clips data/clips.jsonl --annotations data/annotations.jsonl --reviewer my-reviewer-id --review-queue data/review-v002.jsonl
```

The queue ranks model disagreement on reviewed negatives, uncertain predictions
and unreviewed high-threat clips. High confidence does not imply a false positive
or accepted pseudo-label. The reviewer decides, creates a new annotation revision,
then exports a new manifest and retrains. Existing model/source/split versions
remain unchanged. `DatasetTools/augmentation/README.md` explains the boundary
between original timing and training augmentation.

## Verification and current limits

```powershell
python -m unittest discover -s Tests -p "dataset_*.py" -v
```

Tests use generated shapes/textures and synthetic frame bundles explicitly
labeled **SYNTHETIC TEST ONLY**. They verify actual FFmpeg/OpenCV ingestion,
timestamps, camera-flow behavior, clip extraction, reviewed-label export,
no-contact censoring, leakage protection and review queues. They are not Sekiro
gameplay, trained boss data, annotation accuracy or live Dodge measurements.
Tk UI interaction needs a graphical session; headless tests cover its shared
annotation journal/contract and can exercise the GUI when a display is present.

The current automatic miner follows a configurable combat ROI and camera motion,
not a trained boss/Wolf tracker or verified weapon/pose model. Long videos with
strong occlusion, zoom, cuts or HUD artifacts still need review. A source catalog
does not resolve video-download permission, and many no-hit videos cannot provide
observed-contact TTI. These limitations do not block model code, export/runtime
integration, or continued acquisition of permitted footage.

Primary API references checked during implementation:
[FFprobe output/entries](https://ffmpeg.org/ffprobe.html),
[OpenCV optical flow](https://docs.opencv.org/4.12.0/d4/dee/tutorial_optical_flow.html),
[OpenCV affine RANSAC](https://docs.opencv.org/4.12.0/d9/d0c/group__calib3d.html),
and [FFmpeg concat demuxer](https://ffmpeg.org/ffmpeg-formats.html#concat-1).
Context7 was also used to check OpenCV feature tracking APIs.
