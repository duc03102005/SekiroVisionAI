# Model experiment contract

## Candidate matrix

| Candidate | Reason to evaluate | Main evidence needed |
|---|---|---|
| ROI encoder + causal TCN | Parallel temporal operations, bounded receptive field | Causal padding, stage age, early-phase precision |
| ROI encoder + GRU | Compact recurrent state | State reset semantics, long-run drift, ONNX state IO |
| ROI encoder + LSTM | Alternative recurrent baseline | Extra state/cost versus measurable quality gain |
| Lightweight causal transformer | Longer or irregular temporal relationships | Causal mask, bounded cache, operator support and tail latency |
| Adapted VideoMAE features/teacher | Video pretraining transfer | Domain transfer, no future leakage, live compute feasibility |
| Pose/weapon/flow augmentation | Relative geometry and motion cues | Camera-motion robustness, missing-point masks, total extra cost |

Do not instantiate all combinations without purpose. Start with a small reproducible baseline, then use errors to choose the next comparison. Complete the user's resolution/window benchmarks for shortlisted models before final selection.

## One experiment row

Record model/data/split hashes, training seed, FPS/sampling interval, B/T/H/W, preprocessing version, encoder refresh rate, hidden-state policy, precision, ORT/opset/provider/driver versions, warmup, RTX 3070/game settings, duration and stage latency distributions. Include memory and game FPS changes.

Evaluate event precision/recall/F1 with one-to-one matching at predefined temporal tolerance, not only frame accuracy. A long IDLE segment must not inflate attack scores. Report PR curves, per-class counts and clustered confidence intervals where sample size allows.

Report false Dodge decisions per non-threat minute and false decisions divided by all decisions; keep the denominators separate. Report missed threats divided by evaluable threat events, TTI MAE/median/p95 absolute error, signed bias, interval coverage and unsupported/censored target counts. Evaluate decisions in replay/shadow mode before enabling input.

Replay the full causal capture-to-decision timing. A model using stale frames can have low inference latency and still miss the dodge window. Do not add unrelated per-stage quantiles to estimate end-to-end quantiles.

## Leakage checks

Group full recordings, repeated encodes, overlapping clips and duplicate uploads before splitting. Keep a dedicated enemy-family holdout and report pretrained exposure when known. Appearance leakage, HUD/boss-name shortcuts, no-hit runner behavior and post-impact damage cues all require ablations or temporal masking. A classifier that memorizes boss identity has not demonstrated unseen-enemy motion generalization.
