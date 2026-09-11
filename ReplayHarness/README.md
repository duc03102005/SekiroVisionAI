# Native production replay

`ReplayHarness` links the same `CombatPipeline` library as `SekiroVisionAI.exe`.
There is one implementation of target detection/tracking, dynamic ROI selection,
`TemporalModel` RGB preprocessing and causal history, `TemporalDecision` threat
tokens, and the direction chooser. Replay does not reproduce these in Python.
The native executable never calls `SendInput`; accepted requests are explicitly
recorded as `SIMULATED_INPUT_ACCEPTED` using the shared `DispatchGuard`.

## Run on Windows

This is a developer evaluation tool; the ordinary application needs no terminal.
Run it from a build/package containing its native runtime DLLs:

```powershell
.\ReplayHarness.exe --video fight.mp4 --model Models\production\model.onnx --labels reviewed.csv --out-dir evaluation\fight
```

The sibling `targets.onnx` is selected automatically; `--target-model` selects
another version for controlled comparisons. `--provider CPU`, `DirectML`, or
`CUDA` selects the requested provider. Reports show the provider actually loaded.
Missing or unsupported target evidence still allows model observations but
prevents an automatic action in the default production configuration.

Windows Media Foundation decodes local MP4/video files using installed native
codecs. RGB32 rows are copied using their actual signed stride; the decoded color
frame stays at source resolution. `derive_gray` is the same helper used by
capture. A changing media type creates a new capture generation. Invalid or
nonmonotonic presentation timestamps are rejected, never relabeled as fresh.
Windows editions lacking Media Foundation/codecs report the native error.

## Time and output contract

Decoder presentation timestamps are preserved and the first decoder PTS is
recorded. Media Foundation and FFprobe can expose different constant clock
origins for the same frame; frame zero, then the preserved relative intervals,
defines the annotation timeline. Replay time zero is the first decoded PTS; an
internal monotonic origin of 1000 ms permits the same positive-time contracts as
live QPC. A bounded latest-frame mailbox replaces frames that arrive while
inference is busy. Future frames do not enter model history early.

Default processing time is measured in the native pipeline. File decoding occurs
outside the simulated capture clock. `--capture-latency-ms` and
`--input-latency-ms` are explicit assumptions, initially zero; they are not
measured WGC, game, display, or RTX 3070 latency. Input delay is applied once and
the same source-age/cooldown and feasible TTI interval are checked at simulated
dispatch. `--deterministic-processing-ms` substitutes a declared timing fixture
for reproducible software regression tests only.

The output directory contains:

- `trace.jsonl`: source identity, ROI/target state, actual model/provider,
  supported attack/threat/TTI values, separate attack and Dodge direction,
  processing/age, decision reason, and simulated input acceptance/rejection.
- `run.json`: source/model SHA-256, frame replacements, timing assumptions,
  native processing distributions and model eligibility.
- `acceptance.json`: produced only when reviewed labels were supplied.

Attack-direction softmax is explicitly uncalibrated; it is not the probability
that a direction is safe. A provisional lateral Dodge has zero claimed safety
confidence and `PROVISIONAL_SIDE_DODGE_SAFETY_UNKNOWN`. Verified escape evidence
can select A/D/S/W, while sweeps/AOE and high-confidence radial attacks abstain.
The Debug switches `--cv-debug`, `--allow-provisional-targets`, and `--roi` are
recorded in the trace. They are not evidence that missing semantic models work.

## Reviewed acceptance labels

CSV columns:

```csv
event_id,kind,start_ms,end_ms,impact_min_ms,impact_max_ms,lead_min_ms,lead_max_ms,attack_class,evidence,reviewed
```

`kind` is `THREAT`, `NON_THREAT`, or `UNKNOWN`. All times use the video's zero-based
replay timeline. Every row has a unique event/strike ID and a reviewed observation
interval. A `THREAT` is timing-evaluable only with `reviewed=true` and
`evidence=OBSERVED_CONTACT`. Impact bounds describe reviewed contact evidence;
lead bounds describe an independently established acceptable input window. A
no-hit clip does not supply observed contact simply because a dodge was visible.
Censored/unknown-contact attacks are excluded from timing success denominators.

For contact interval `[impact_min, impact_max]`, acceptance requires the complete
interval to fit the lead window:

```text
earliest input = impact_max - lead_max
latest input   = impact_min - lead_min
```

Both bounds must lie in the reviewed observation interval. Empty/inverted
windows, non-finite times, duplicate IDs and contradictory reviewed negative
intervals are rejected. There are no per-boss animation timers in this policy.

Correct-window decisions use maximum one-to-one event matching, including
overlapping combo-strike windows. Remaining decisions associate with an unmatched
attack only within its reviewed interval, in increasing distance to its accepted
window. Outcomes are `SUCCESS`, `EARLY`, `LATE`, `MISSED`, or `FALSE_DODGE`.
Extra decisions within a consumed labeled attack count as false duplicates.
Actions in unlabeled or contact-censored footage are `UNEVALUATED`, not invented
negatives. False Dodge per non-threat minute uses only the union of explicitly
reviewed `NON_THREAT` intervals and decisions inside those intervals.

An empty trace, untrained model, no accepted actions, missing labels, or a tiny
fixture never establishes gameplay accuracy or a functional all-boss release.
The report preserves the denominator and exclusions so these cases stay visible.

## Portable native tests on Linux

An installed ONNX Runtime CPU SDK allows the exact native shared library and
replay executable to run on Linux. The optional offline converter uses FFmpeg to
write a selected contiguous interval without changing pixels, FPS or source PTS:

```bash
python tools/video_to_replay.py source.mp4 artifacts/fight.svr --start-frame 300 --frames 120
ReplayHarness --video artifacts/fight.svr --model Models/production/model.onnx --out-dir artifacts/native-fight
```

The `.svr.source.json` sidecar records original media SHA-256, decoded source-frame
range and original PTS. The bounded output limit prevents accidental multi-hour
raw-video expansion. This conversion is development tooling, not a Python model
runtime or a dependency of the shipped Windows application.

The little-endian fixture format is `SVRRAW01`, `uint32 width,height`, then repeated
`int64 original_pts_100ns`, `uint64 generation,sequence`, tightly packed BGRA8.
The stream has no invented duration or inferred frame timestamps.

`tools/make_replay_fixture.py out/testdata` generates declared synthetic RGB
quadrants with a five-bit original-frame index as both `.svr` and H.264 MP4.
Native tests check real MP4 decode, color channels/orientation, exact original
frame order through H.264 reordering, every relative PTS, identical model
preprocessing/history, non-gameplay
model rejection, cancellation/duplicate suppression, source staleness and
five-way acceptance. The decoder's original presentation clock is preserved;
it need not begin at zero. [Windows CI run 34543602025](https://github.com/duc03102005/SekiroVisionAI/actions/runs/34543602025)
observed all 30 frames, first PTS 133.333 ms, last PTS 1100 ms and the expected
966.667 ms span. This evidence invalidated an earlier test's zero-origin
assumption. No decoder timestamp was changed to repair that test. These are
software/codec tests, not gameplay measurements.

Microsoft references: [Source Reader processing](https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader),
[Lock2D row orientation](https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nf-mfobjects-imf2dbuffer-lock2d),
[image stride](https://learn.microsoft.com/en-us/windows/win32/medfound/image-stride).
