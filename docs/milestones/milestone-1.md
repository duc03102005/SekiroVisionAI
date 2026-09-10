# Milestone 1: NOT PASS — hardware measurements pending

Updated 2026-09-10. M0 is [PASS](milestone-0.md); architecture research and the first capture implementation are on `feature/capture-engine`.

| Requirement | Status | Evidence / next step |
|---|---|---|
| Runtime/API research | COMPLETE | ADR-001 and ADR-002; official Microsoft API pages and SDK header inspected |
| HWND capture to owned GPU texture | IMPLEMENTED, Windows execution unverified | WGC + D3D11, four pool buffers, three destination slots, retained source leases |
| UI selection, Start/Stop and measured counters | IMPLEMENTED, Windows execution unverified | Native capture probe; no simulated performance values |
| Portable timing invariants | PASS locally | GCC 13.3, C++20; missing/future clocks, bounded histories, percentiles, cadence and staleness |
| Windows Release compilation and tests | PENDING CI | Windows Actions workflow builds actual native target and exports an unsigned executable |
| >=60 FPS stable; no major loss | NOT MEASURED | Requires Sekiro on the user's RTX 3070 and independent source-cadence evidence |
| Capture latency measured | INSTRUMENTED, NOT MEASURED | QPC matched rows, GPU timestamp/disjoint queries, observed ready age |
| CPU/GPU usage and memory stability | NOT MEASURED | UI CPU/working set plus external GPU/baseline measurement required |
| Resize/close/device/stale behavior | IMPLEMENTED, NOT VERIFIED LIVE | Run separate scenarios from the runbook |
| Pixel validity/HDR/overlay exclusion | NOT VERIFIED | SDR only; preview/black-content detector and overlay checks remain follow-up work |
| Full M1 acceptance | NOT PASS | No live Windows/Sekiro/RTX 3070 benchmark has been performed |

See [build and measurement instructions](../capture-runbook.md). Windows CI, when successful, will establish build/test compatibility only. Do not advance to M2 or label the capture prototype production-ready until M1's hardware criteria and failure handling have evidence.
