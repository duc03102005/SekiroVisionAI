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
