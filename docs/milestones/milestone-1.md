# Milestone 1: NOT PASS — hardware measurements pending

**Historical report.** The user's later [MVP direction](../mvp-scope.md) removed all milestone progress gates. Auto Dodge development proceeds without waiting for the hardware measurements below.

Updated 2026-09-10. M0 is [PASS](milestone-0.md); architecture research and the first capture implementation are on `feature/capture-engine`.

| Requirement | Status | Evidence / next step |
|---|---|---|
| Runtime/API research | COMPLETE | ADR-001 and ADR-002; official Microsoft API pages and SDK header inspected |
| HWND capture to owned GPU texture | IMPLEMENTED, Windows execution unverified | WGC + D3D11, four pool buffers, three destination slots, retained source leases |
| UI selection, Start/Stop and measured counters | IMPLEMENTED, Windows execution unverified | Native capture probe; no simulated performance values |
| Portable timing invariants | PASS locally | GCC 13.3, C++20; missing/future clocks, bounded histories, percentiles, cadence and staleness |
| Windows Release compilation and tests | PASS | [Windows Actions run 34488924494](https://github.com/duc03102005/SekiroVisionAI/actions/runs/34488924494), source commit 3cd23db; MSVC 19.44.35228.0, SDK 10.0.26100.0; CTest 1/1 |
| >=60 FPS stable; no major loss | NOT MEASURED | Requires Sekiro on the user's RTX 3070 and independent source-cadence evidence |
| Capture latency measured | INSTRUMENTED, NOT MEASURED | QPC matched rows, GPU timestamp/disjoint queries, observed ready age |
| CPU/GPU usage and memory stability | NOT MEASURED | UI CPU/working set plus external GPU/baseline measurement required |
| Resize/close/device/stale behavior | IMPLEMENTED, NOT VERIFIED LIVE | Run separate scenarios from the runbook |
| Pixel validity/HDR/overlay exclusion | NOT VERIFIED | SDR only; preview/black-content detector and overlay checks remain follow-up work |
| Full M1 acceptance | NOT PASS | No live Windows/Sekiro/RTX 3070 benchmark has been performed |

See [build and measurement instructions](../capture-runbook.md), [machine-readable build evidence](../research/evidence/milestone-1-build.json), and [draft PR #1](https://github.com/duc03102005/SekiroVisionAI/pull/1). The [unsigned x64 probe artifact](https://github.com/duc03102005/SekiroVisionAI/actions/runs/34488924494/artifacts/10156890561) was generated successfully (retained until 2026-09-24). These are historical compilation/test results, not progress gates under the current MVP direction.
