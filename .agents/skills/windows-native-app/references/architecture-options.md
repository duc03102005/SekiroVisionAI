# Post-M0 architecture comparison

No option is selected in Milestone 0. The following matrix defines the comparison to run once the user's skills gate passes.

| Option | Potential strengths | Risks to measure |
|---|---|---|
| C++ core and C++ UI/WinRT | Direct native ownership and fewer language boundaries | UI iteration cost, error/lifetime complexity, packaging dependencies |
| C# WinUI 3 frontend + C++ core | Managed UI tooling with GPU scheduling retained natively | ABI/callback ownership, teardown races, extra build/package coordination |
| C# WinUI 3 with native API bindings | Single main application language | Per-frame allocations/GC, COM lifetimes, binding availability, native GPU interop complexity |

Performance priority does not by itself prove C++ wins every stage. Benchmark resource ownership, transfer count, allocation pressure, tail latency and game contention under the same workload. A hybrid only helps if the ABI keeps expensive frame data in the core.

ADR evidence should include available maintained bindings/packages and versions; minimal proof of WGC texture → preprocessing → selected runtime compatibility; intended UI update rate; exception/error contract; clean deployment footprint; and remaining risks. Do not invent live performance results when hardware is unavailable.

Keep the first implementation focused on M1: capture to texture with metrics. Do not add detector, temporal model, keyboard injection or a fake finished dashboard while trying to prove capture stability.
