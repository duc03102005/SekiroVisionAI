# Temporal pipeline validation — 2026-09-10

## Windows native package

Source commit: `fc921637d54477fd14b44fbfe85fcf85f2c01a26`.
[Windows workflow run](https://github.com/duc03102005/SekiroVisionAI/actions/runs/34536285368)
completed successfully. The unsigned x64 package is available as its
[Windows artifact](https://github.com/duc03102005/SekiroVisionAI/actions/runs/34536285368/artifacts/10175604073)
(16,791,241 bytes; SHA256
`bad36eda2a802ad128ceaaf915593b1e6e966d6991ad1f7d654721dc37f7f204`).

CMake3.31.6 / MSVC / Windows2022 runner compiled the native app without C++ compiler
warnings. CTest passed6/6: capture timing, CV Dodge behavior, temporal model policy,
actual WARP color-readback shader/staging, native ORT CPU model load/run/sampling,
and actual WIC JPEG recording roundtrip. Both the build-directory and packaged
executable initialized UI/hotkeys/watchdog and exited normally. Smoke mode does
not send keys. Packaged files include ORT, DirectML, app-local VC++ CRT, licenses,
Vietnamese guide and a hash manifest. No trained gameplay model is included.

This validates compilation and exercised native plumbing. The hosted runner did
not run Sekiro or an RTX3070, and native ONNX fixture tests use CPU execution.
DirectML/CUDA game-load performance, actual game input reception and a successful
in-game Dodge remain unmeasured.

## Offline tools and model checks

- 9/9 dataset tests passed locally with real FFmpeg/OpenCV decode/encode. Tests
  cover generated-video ingestion, camera compensation, clip extraction, VFR
  source mapping/duplicates, reviewed annotation/export, no-hit masks, grouping,
  review queues and native recorder import. These are synthetic test fixtures.
- 10/10 training tests passed, including actual video loading, masks, causality,
  leakage rejection and loss gradients.
- 3/3 coverage tests passed: references/missing files stay zero; reviewed media
  counts deduplicate/union time; later rejected labels cannot inflate coverage.
- All three architectures took 80 optimizer steps, reduced their multi-task loss,
  exported ONNX and passed3 numerical parity cases each. Eager/exported outputs
  agree within1e-4 relative/absolute tolerances. Local native C++ ORT1.25 also
  loaded and ran all three exports; they remain marked SYNTHETIC_ONLY/no auto.

Exact synthetic measurements and code hashes are in
[synthetic-cpu-2026-09-10.json](../Evaluation/reports/synthetic-cpu-2026-09-10.json).
Training uses moving tiles, not boss footage; timings are a shared Linux CPU
host, batch1/T16/RGB320 inference only. They are not attack accuracy, RTX latency
or capture-to-input measurements, and do not select a production winner.

Initial Linux CI exposed an undeclared FFmpeg dependency. The workflow now
explicitly installs FFmpeg and Tk/Xvfb. A separate real Tk initialization,
playback/seek/mark/save smoke test runs under Xvfb; it does not validate human
annotation accuracy. Latest workflow results are attached to PR#3.

## What remains zero/unknown

Acquired gameplay sources0; reviewed gameplay clips0; gameplay-trained models0.
Same-boss/unseen-source and held-out-boss gameplay metrics, false Dodge rate,
missed attacks, contact TTI errors and live avoidance success are unavailable.
This is not a milestone gate: early supervised training and native integration
can proceed as soon as any small authorized reviewed dataset exists.
