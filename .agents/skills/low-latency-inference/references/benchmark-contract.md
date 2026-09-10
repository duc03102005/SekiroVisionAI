# Live inference benchmark contract

Benchmark one running game, one ordered temporal stream, batch=1 and a continuous bounded inference schedule. Test the actual RTX 3070 while it also renders Sekiro. Synthetic tensors and video replay can isolate costs, but label them separately from live performance.

Required metadata: Windows/driver/build configuration, GPU and adapter, game resolution/settings/source cadence, model/weights hash, provider and runtime versions, precision, input shape, history length/sample interval, allocation/stream policy, queue capacity, warmup, measured duration and trace hash.

Record per-frame source/callback/preprocess-ready/inference-ready/decision/input-submission timestamps and their clock domains. At M10 distinguish requested input, successfully submitted input and observed in-game response; `SendInput` returning success does not measure the game's input-consumption time. Earlier milestones leave input latency unmeasured.

Output table fields:

| Field | Required distinction |
|---|---|
| stage CPU/GPU ms | Host work versus actual device work |
| e2e ms | Paired frame source-to-decision or source-to-input endpoint |
| frame age at decision | Includes capture delay and queueing |
| p50/p95/p99/max | Distribution, not just mean |
| startup/engine build | Separate from warmed inference |
| dropped/skipped work | Explicit denominator and cause |
| game FPS delta | Same route/configuration game-only comparison |
| event precision/recall | Same frozen source-group test set |
| false decisions/minute | Non-threat exposure time and decision count |
| TTI error/coverage | Evaluable labels and uncertainty intervals |

Stage durations overlap under async execution, so neither summing quantiles nor blindly summing overlapping spans gives E2E latency. Preserve matched timestamps. Report measurement instrumentation overhead. Separate rendering/VRAM contention from model execution time.

Do not optimize a provider until a trace identifies its bottleneck. Reject an optimization that breaches the approved false-Dodge budget even if it improves mean latency. Performance thresholds are proposed before the run and selected using validation data; a held-out test result is not a tuning set.
