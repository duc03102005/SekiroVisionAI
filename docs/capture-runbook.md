# M1 capture probe: build and measure

This is a native Windows diagnostic application for capture only. It has no trained model, overlay or keyboard injection. The first test scope is Windows x64 with Sekiro in SDR windowed/borderless mode, preferably on the RTX 3070. HDR, exclusive fullscreen and pixel-content validity are not yet verified. Maximum content area is 3840 × 2160 pixels (other aspect ratios allowed, each dimension <=8192).

## Build or download

Install Visual Studio 2022 with Desktop development with C++, MSVC v143, Windows SDK **10.0.26100.0**, and CMake >=3.25. The preset selects the SDK explicitly. From the repository root:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

Open `out/build/windows-x64/Release/SekiroCaptureProbe.exe`. It is a GUI executable with the static MSVC CRT; no Python or .NET is needed to run it. Python is only needed for repository validation and optional trace analysis. This prototype is unsigned. Offline launch and clean-machine packaging still need a Windows test.

The **Windows capture build** GitHub Actions run produces an artifact named `SekiroCaptureProbe-windows-x64-<commit>`, retained for 14 days. Download it from a successful run and extract the ZIP before opening the EXE. It includes this runbook. A successful hosted build establishes compilation and timing-test results, not live capture performance.

## Operate

1. Launch Sekiro and use SDR windowed or borderless mode. Keep the game on one monitor for the session. The capture device is selected from the hardware adapter owning that monitor; inspect its actual name/LUID in the probe.
2. Open the probe, select the visible `sekiro.exe` window, and click **Start**. The selection is checked against PID and process creation time. No title-only or foreground-window selection is used.
3. For a timing run, enable **Record timing trace** before Start. Recording stores bounded numeric timing rows in memory; no screenshots or game video are recorded. Recording is off by default.
4. Leave capture running while playing the selected test route. Click **Stop** when done. Stop is asynchronous; the UI remains active while the worker drains GPU copies.
5. Click **Export trace** after capture has stopped. Save both the CSV and its automatically generated `.csv.meta.json` sidecar. Starting another session replaces the previous in-memory trace, so export first.

The UI samples process CPU/working set and capture counters every 250 ms. GPU utilization is explicitly unavailable; collect it externally. The FPS display takes two seconds to fill and falls to zero after frames cease. It does not count distinct game presents. Latency percentiles use the latest 2,048 valid measurements; analyze the CSV for the entire interval.

## Trace interpretation

| Field | Meaning |
|---|---|
| sequence | Monotonic number of a frame successfully drained from WGC, starting at 1 per session |
| generation | Ring/content-size generation; changes after resize |
| source_qpc_ms | WGC SystemRelativeTime, converted from 100 ns to QPC milliseconds |
| dequeue_qpc_ms | Worker time immediately after acquiring this frame |
| submit_qpc_ms | CPU time before submitting the GPU copy; not GPU completion |
| ready_observed_qpc_ms | First observed GPU event completion; includes up to scheduling/polling delay |
| gpu_copy_ms | GPU timestamps with a valid disjoint interval/frequency; blank if unavailable |
| width, height, slot | Valid content rectangle and owned destination slot; -1 if not submitted |
| outcome | copied, superseded, ring_busy, resize, invalid or abandoned |

Dequeue age = dequeue minus source. Observed ready age = ready minus source, an upper bound on when the copy became usable. GPU timestamp duration is separate from CPU age. Do not add stage percentiles into an end-to-end percentile. Callback notifications can coalesce relative to drained frames; their session-wide gap count is not an exact count of compositor drops. WGC delivery does not establish unique game FPS.

Trace capacity is 120,000 received frames, approximately 16.7 minutes at 120 FPS or 33.3 minutes at 60 FPS. On saturation the UI reports FULL and counters continue, but the trace is incomplete. Do not use a truncated trace for acceptance. Allocation is bounded to four pool buffers, three owned textures, timing histories and the capped trace; driver allocation and game resource usage still need measurement.

## Required live acceptance run

The proposed thresholds come from the reviewed [frame contract](../.agents/skills/windows-gpu-capture/references/frame-contract.md), not an API guarantee. Record deviations before testing.

1. Record commit/executable hash, Windows build, NVIDIA driver, SDK/compiler from CI, CPU/RAM, adapter/LUID, resolution, display refresh, HDR off, game version/settings, window mode, power mode and source-FPS measurement method. Use Release x64.
2. Record a matched game-only baseline and the same active-combat route with capture enabled. Source cadence must independently sustain >=60 FPS; 120 Hz monitor refresh alone is insufficient.
3. Enable trace. Warm up at least 30 seconds, then measure 10 minutes. Stop after at least 10 minutes 35 seconds to leave a small end margin. Keep resize/minimize and other planned discontinuities out of this steady-state run.
4. Record game FPS/frame times, process CPU, GPU utilization, private/working-set memory and GPU memory over the interval. UI process memory is a spot measurement, not proof of no growth. Preserve external monitoring evidence alongside the trace.
5. Require delivered cadence >=60 FPS with <=1% tolerance for 59.94/quantization, including ten-second windows; intentional ingestion loss <=1%; no unexplained delivery gap over 100 ms; bounded ring allocation and no continuing memory growth or crash loop. Report latency p50/p95/p99/max, even though an absolute age budget is not yet approved. Investigate performance impact against the baseline.
6. Analyze the timing trace using the standard-library tool:

```powershell
python tools/analyze_capture_trace.py C:/benchmarks/sekiro-capture.csv > C:/benchmarks/capture-summary.json
Get-FileHash C:/benchmarks/sekiro-capture.csv -Algorithm SHA256
Get-FileHash out/build/windows-x64/Release/SekiroCaptureProbe.exe -Algorithm SHA256
```

The analyzer reports capture criteria only and deliberately leaves the full milestone NOT_EVALUATED. Supply the independent source cadence, resource-usage and lifecycle results before writing an overall PASS/FAIL. Raw traces belong outside Git; commit only a concise result with evidence hashes/locations and configuration.

## Separate lifecycle checks

| Scenario | Current intended behavior | Result to record |
|---|---|---|
| Resize within supported area | Retire in-flight copies, Recreate, allocate a new generation | New dimensions/generation, no stale reuse, bounded memory |
| Minimize | Stop with an explicit fault; restore and Start a fresh session | No stale FPS or old session reuse |
| Alt-tab without minimizing | Continue HWND capture; no input is sent | Delivery, source age and any presentation-mode stalls |
| Move to another monitor | Explicit fault; restart to select the adapter again | Actual adapter/LUID after restart |
| Game exit/restart or HWND change | Close/fault; refresh and select again | New PID/creation identity, no automatic attachment |
| Stop/window close | Revoke capture, drain GPU, then close resources | UI responds; no process left behind |
| Capture stall/device removal | Explicit fault and bounded GPU drain attempt | HRESULT and reason; restart after resolving device/source issue |
| HDR/exclusive fullscreen | Outside initial supported test scope | Separate experiment, not a steady-state PASS |

The prototype does not recover automatically from faults, render a texture preview, detect black pixels or prove overlay exclusion. These remain explicit M1 evidence gaps/follow-ups. There is no frame consumer after the owned copy, so no texture or prediction escapes a discontinued generation. GPU/CUDA interop, preprocessing and a versioned C ABI are later work; retaining a D3D texture is not an inference input implementation.
