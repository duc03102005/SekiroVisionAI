# Frame ownership and capture evidence

## Frame descriptor

Each descriptor identifies a capture session, monotonic frame sequence, source timestamp (if available), callback QPC timestamp, content dimensions, pixel format/color space, adapter LUID, crop/resize transform, resource slot/generation, and the completion token that permits reuse. Carry validity flags and discontinuity reason. Do not expose raw texture pointers without a defined lease contract.

The descriptor is immutable after publication. Consumers reject old session/generation IDs. Reuse a slot only after every registered GPU consumer completes. A bounded texture ring for ingestion is separate from the temporal history of features; retaining 32 full-resolution capture leases is not the default sequence representation.

For asynchronous D3D/CUDA interop, document resource registration lifetime, map/unmap ordering and which stream owns each operation. Release/unregister resources after completion. A resize may allocate a new generation while the previous one retires; cap both generations' memory during the transition.

## Clocks and measurements

| Measurement | Meaning |
|---|---|
| callback age | Callback QPC minus converted WGC source timestamp, only after validating their time basis |
| GPU stage duration | GPU end/start timestamps with valid frequency and no disjoint interval |
| consumer age | Time the frame is usable by its consumer minus source timestamp |
| ingress drops | Received frames intentionally discarded before processing |
| inference skips | Usable frames not selected by the inference scheduler |
| callback gaps | Observed time gaps, not an exact count of unobserved compositor drops |
| copy traffic estimate | Width × height × bytes/pixel × actual copies per second; distinguish from a hardware counter |

Do not add stage p95 values and call that total p95. Calculate total latency from matched frame traces. Distinguish unavailable metrics from zero. A visually repeated frame may be a genuinely static scene; do not fabricate exact game-present counts from WGC callbacks.

## Proposed M1 acceptance protocol

These are project acceptance proposals that operationalize “stable” and “no major drops”; they are not published WGC performance guarantees.

- Run Windows x64 with Sekiro and the RTX 3070. Record game settings, power mode, adapter, display and software versions. Use a reproducible active-combat route/recording plan.
- Warm up 30 seconds, then record 10 minutes with capture enabled, no input automation. Record a matched game-only baseline. Exclude planned resize/focus-loss intervals from steady-state metrics and report them separately.
- The source must sustain at least 60 FPS. Report total delivered FPS and ten-second windows against the measured source cadence. Minimum 60 FPS target allows at most 1% cadence tolerance for 59.94/timestamp quantization; do not round a material shortfall into PASS. If the source is below target, mark the acceptance run inconclusive/FAIL, identify the bottleneck and preserve results.
- Proposed steady-state limits: intentional ingestion loss ≤1%, no unexplained delivery stall over 100 ms, bounded allocated ring slots, no continuing memory growth, no crash/device-removal loop. State precisely how those counts were obtained.
- Report frame age p50/p95/p99 and maximum, even when no absolute capture-age budget has yet been accepted. Preferred 120 FPS requires an independently demonstrated 120 FPS source; it is not an M1 minimum.
- Exercise resize, minimize/restore, alt-tab, game exit/restart and overlay toggle; no stale descriptor may escape recovery. Report each scenario separately from the steady run.

At the end write PASS/FAIL, configuration, duration, raw trace path/hash and which criterion failed. Never replace the hardware run with this document.
