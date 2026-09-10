---
name: windows-native-app
description: Compare and structure SekiroVisionAI native Windows C++/C# runtimes, WinUI 3 frontend interop, packaging, configuration, logging and performance diagnostics.
---

# Windows native application

Before application code, check M0 in `AGENTS.md`. This skill defines an architecture decision process, not a final language choice. Read [architecture-options.md](references/architecture-options.md) when producing the post-M0 ADRs.

## Decide with evidence

Compare native C++ capture/D3D/inference with native UI, C# WinUI 3 frontend plus C++ core, and C#-only WinUI with native API bindings. Use current official documentation, actual package headers and Context7. Separate C++ engine costs, UI costs and interop crossings in a proposed benchmark.

After M0 passes, record ADR-001 runtime/language, ADR-002 capture API, ADR-003 inference runtime, ADR-004 temporal model and ADR-005 overlay architecture as their decisions become supported. Each ADR needs Context, Options, Decision, Reason, Tradeoffs, dated source links and validation/follow-up evidence. Do not fill them with a foregone answer merely to create files.

Favor a narrow ABI if choosing a hybrid: opaque engine handles, versioned POD structs with explicit size/alignment, ownership rules, UTF-8/UTF-16 contract, status codes and teardown/callback cancellation. Do not pass STL containers or uncaught C++ exceptions across a C# P/Invoke boundary. Keep delegates alive while native code may call them; release after native shutdown acknowledgement.

Keep textures, tensor buffers, capture/inference threads and frame scheduling inside the core when feasible. Send throttled immutable metrics/state to the UI, not a full-frame CPU copy per binding update. UI dispatcher stalls must not block capture or emergency release.

## Native build structure

- Use pinned MSVC/Windows SDK and CMake presets for the native core. Do not expect MinGW `gcc.exe` to provide the required Windows/MSVC setup.
- Use pinned Windows App SDK/.NET packages when WinUI/C# is selected. Distinguish Debug diagnostics from Release benchmark builds and identify architecture x64 explicitly.
- Python remains training/dataset/export/evaluation tooling. A source-build tool needing Python does not imply a Python final runtime.
- Create the user's component structure only after M0 passes and as milestones require it. Keep CaptureEngine, ModelRuntime, TemporalEngine, ThreatEngine, InputEngine, Overlay and UI responsibilities explicit.
- Use CI to check portable invariants and Windows compilation when introduced. Generic hosted runners do not validate RTX 3070 live-game timing. Do not mark M1/M10 PASS from CI alone.

## UI and lifecycle

Provide Start/Stop, Auto Dodge and Overlay toggles, threshold/model/performance selection, debug mode, recording, logging and benchmark actions as features arrive. Show actual capture status/FPS, GPU/provider, prediction/TTI uncertainty, frame age and decision reason. Use “unavailable” or “not measured” until data exists. No hardcoded 120 FPS/97%/6.2 ms status values.

Default to Auto Dodge OFF. Make Stop/emergency behavior immediately visible. Keep the overlay optional, non-activating and click-through where intended, with correct DPI/client-coordinate transforms. Validate overlay exclusion from model input and impact on game frame times.

Handle capture closure, device removal, model load failure and worker termination with explicit state transitions. Centralize cancellation; avoid UI-thread joins on long GPU work. Do not rely on crash handlers alone for key release.

## Configuration, logs and packaging

Version configuration schemas and validate thresholds, time units, key mappings and model paths. Save per-user data in an appropriate application-data location, not the install directory. Use bounded asynchronous logs with session IDs, error/HRESULT/provider information and trace hashes; make recording opt-in and bound disk usage.

Compare unpackaged deployment and MSIX against the selected Windows App SDK/native dependency requirements. Package actual MSVC/runtime dependencies and the intended provider DLLs according to their licenses; document GPU driver prerequisites. An MSIX full-trust desktop app is not automatically AppContainer-sandboxed.

Produce Release artifacts reproducibly, with version/architecture/model manifest and dependency notices. Use legitimate signing when credentials are available; never claim an unsigned executable is signed. Test on a clean Windows machine, including offline launch, missing dependency/model, high DPI, game exit and crash recovery. A successful build is not a packaging acceptance test.

## Primary sources

- [Windows App SDK](https://github.com/microsoft/WindowsAppSDK)
- [Windows App SDK samples](https://github.com/microsoft/WindowsAppSDK-Samples)
- [WinUI review skill provenance](../winui-code-review/LICENSE)
- [Windows SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)

Use the imported WinUI review skill only if the frontend uses WinUI. Its manual review does not install Microsoft analyzers or upstream workflow scripts.
