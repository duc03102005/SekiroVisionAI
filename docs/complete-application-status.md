# Complete application implementation

The active branch is `feature/complete-application`, based on `fa82502a1732dea6bcb3f4c31e1b0ba8f4e47526`. The user requested a finished native Windows application, not another diagnostic handoff. Development proceeds in parallel without milestone approval gates.

## Required final behavior

`SekiroVisionAI.exe` discovers the game, starts Windows Graphics Capture, automatically tracks combat, loads a bundled gameplay-trained temporal model, predicts attack/threat/time to impact, selects a direction and submits one controlled Dodge per confirmed threat. F8 enables/disables; F9 cancels and releases keys. Normal users do not install Python or choose model files/ROI. Advanced controls remain available for development.

## Evidence boundary

The previous 0.3 Windows build compiled and passed native capture, ONNX, recording, decision and startup tests. Its three exported training fixtures learned synthetic moving tiles, not Sekiro combat. It contains no production gameplay weights. Those results do not establish boss coverage, Dodge timing, false-Dodge rate or RTX 3070 performance.

Current work addresses capture delivery ordering/latency, a shared native production/replay pipeline, automatic visual tracking, attack-direction semantics, input integration tests and a simpler application shell. Acquired footage and reviewed labels must be counted separately from catalog URLs. No synthetic fixture may be promoted into `Models/production` or labeled as a gameplay model.

## Development validation

Portable deterministic tests and real Windows CI tests establish specific software behavior. Prerecorded gameplay replay must execute the same native pipeline as the application and report simulated input, not OS submission. Acceptance scores require independently reviewed attack/contact boundaries; missing contact evidence remains unknown. Live Sekiro response and GPU contention on the user's RTX 3070 cannot be inferred from hosted CI.

The final functional release is not yet available. Internal artifacts will retain explicit development status until supported by real gameplay/model evidence.

## Executed work, 2026-09-10

- Native Windows compilation succeeded. The first complete-application CI run passed the real `SendInput` receiver, F8/F9 cancellation, physical-key preservation and watchdog process-death tests. Full-resolution GPU readback, separate preview, WIC recording and native ONNX tests also passed. An MP4 test incorrectly assumed decoder PTS started at zero; its correction verifies all 30 encoded frame identities and every relative PTS while preserving Media Foundation's original clock. A new Windows run will verify that correction and then run application/package startup checks.
- Portable native tests pass 6/6. Training contracts cover causal histories, head masks and split integrity; target tests cover unknown-role gradients, source/session and duplicate-media leakage, and bounded model outputs.
- Eight acquired videos contain 72,513 decoded frames and approximately 40m10s of actual source footage. Gyoubu and Sword Saint Isshin appear in one source; this is not coverage of all requested bosses. Incidental desktop intervals are excluded from training. Source media is not committed to Git.
- A causal optical-flow fusion model was actually pretrained on 328 histories from seven videos. An attack-phase model was fine-tuned on 242 AI-reviewed anchors. Attack/state heads are supported; threat, contact TTI, attack class and direction heads remain masked. These labels are not independently reviewed human ground truth.
- The LIVE-only phase candidate misses 8/8 positive Gyoubu anchors at the frozen 0.85 threshold. A MobileNetV3 transfer detector finds 1/5 Wolf and 0/5 mounted-enemy boxes in development validation at the unchanged 0.75 threshold. Training fit and native numerical parity do not establish generalization; failures are retained in `Evaluation/reports`.
- The real-gameplay CI workflow reacquires the six licensed LIVE clips, runs actual representation/phase/actor training, preserves immutable candidate weights, and replays a licensed real clip through native Windows code. Candidates remain in a separate development artifact with Auto Dodge disabled because their threat/TTI heads are unsupported.

## Work that still prevents the requested final release

There is no validated all-boss attack/threat/TTI model, no sufficiently general Wolf/enemy detector, no reliable learned safe-direction evidence, and no measured low false-Dodge rate on independently held-out gameplay. No successful Dodge in a live Sekiro instance has been observed here. The full-resolution capture bridge currently reads pixels back to the CPU; direct GPU preprocessing/tensor interop and live RTX 3070 latency/contended-rendering measurements remain unimplemented or unmeasured. These gaps are not milestone approval requests, and synthetic success cannot substitute for them.
