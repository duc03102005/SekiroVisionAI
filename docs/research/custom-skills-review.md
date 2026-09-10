# Custom skills review — 2026-09-10

All six custom entrypoints and their local references were reviewed for required coverage, action boundaries, measurements, resource ownership and misleading assumptions. The reviewed bytes are recorded in [.agents/skill-review.json](../../.agents/skill-review.json). Changing a reviewed skill requires a new review and hash update.

| Skill | Coverage reviewed | Critical constraint |
|---|---|---|
| windows-gpu-capture | WGC/HWND, D3D11/D3D12 choice, texture lease, GPU crop, interop, resize, dropped-frame/latency protocol | No full-frame CPU readback default; no texture pointer without lifetime; no FPS PASS from a display counter |
| realtime-video-ai | Causal TCN/GRU/LSTM/transformer/VideoMAE comparison, sequence timing, flow/pose, export, calibration and generalization | No future frames, no fabricated TTI labels, no direct input from classifier |
| sekiro-dataset-pipeline | Multi-source registry, semi-auto proposals/review, normalized PTS, classes, contact uncertainty and source/enemy splits | No same-source leakage; no-hit avoidance does not reveal exact counterfactual impact |
| low-latency-inference | ORT/CUDA/TensorRT/RTX plugin path, FP16, graphs, I/O Binding, pinned memory, batch-one live measurements | Texture is not a contiguous tensor; explicit copies/synchronization; no invented RTX timing |
| combat-decision-engine | Thresholds, elapsed dwell, hysteresis, event tokens, cooldown, geometry, focus and release safety | One action per strike persists beyond cooldown; unknown evidence abstains |
| windows-native-app | C++/C#/hybrid comparison, WinUI/ABI, MSVC/CMake, UI isolation, configs/logs, crash/packaging | No language decision or application code before M0; UI examples are not real metrics |

The only authored executable is `tools/validate_skills.py`, a standard-library-only local reader. It parses text/JSON, resolves repository references and checks file hashes. It performs no network requests, shell execution, dynamic evaluation, downloads, input injection, package installation or file mutation. The imported skills contain Markdown/LICENSE only; the dataset example is synthetic JSON.

No instruction to read unrelated credentials, override user/system instructions, bypass platform protections, silently install dependencies or transmit gameplay data is included. The emergency/input rules concern the user's requested later M10 behavior, not actions executed in M0.

## Independent behavioral exercise

The skill-creator workflow permits independent forward-testing for complex skills. Two fresh agents separately applied the combat skill to a local synthetic trace; neither sent input or implemented an engine.

The first exercise exposed unclear rules for early-threat dwell, complete prediction defaults and shadow-token consumption. The skill and fixture were corrected. The second exercise used the revised files and explicit policy; see [the trace](evidence/decision-trace.json) and [review outcomes](evidence/decision-outcomes.md).

There are 12 synthetic rows. The revised review proposes one virtual Dodge, rejects duplicate callbacks both during and after cooldown, cancels on focus loss and rejects stale/null/unsafe-direction evidence. This is documentation-level behavioral validation. It is not an automated engine test, a live game result, proof of TTI accuracy or an input release test.

The remaining unspecified choice of source watermark after an invalid packet does not change the exercise decisions; implementation must specify it before M9 trace tests. Windows capture, model training, GPU profiling and key-release behavior remain unimplemented/unmeasured.
