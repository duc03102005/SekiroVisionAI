# ADR-002: Windows.Graphics.Capture with a bounded D3D11 copy ring

Date: 2026-09-10. Status: accepted for M1 implementation; live-game acceptance pending.

## Context and options

The source is one user-selected Sekiro window. Capture must preserve frame lifetime, expose honest age measurements and avoid an unbounded backlog.

| Candidate | Fit | Tradeoff |
|---|---|---|
| WGC + D3D11 | Direct HWND interop, GPU surfaces and source timestamps | Frame-pool ownership, capture restrictions, resize and device handling |
| DXGI Desktop Duplication | Useful if a measured configuration requires output capture | Tracks an output; cropping needs monitor, rotation, DPI, cursor and occlusion handling |
| WGC + D3D12 interop | Potential later GPU pipeline option | Additional synchronization and state complexity without an established M1 benefit |

Choose WGC + D3D11. Desktop Duplication is a documented alternative, not an automatic fallback that silently changes capture semantics. The WGC path performs one GPU copy into an owned texture; it is **not zero-copy** and not yet an ONNX/CUDA tensor path.

## API evidence

All links were consulted 2026-09-10:

- [CreateForWindow](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow) specifies HWND interop and Windows 10 1903 as its minimum.
- [CreateFreeThreaded](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded?view=winrt-26100) removes DispatcherQueue dependence and delivers events on an internal worker. It does not make the D3D immediate context thread-safe.
- [SystemRelativeTime](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.systemrelativetime?view=winrt-26100) is a TimeSpan in the QPC time basis. Convert its 100 ns count and QueryPerformanceCounter to milliseconds; do not compare either with UTC.
- The actual [SDK D3D11 interop header](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/windows.graphics.directx.direct3d11.interop.h) defines device and surface interfaces. Use the SDK declarations, not handwritten COM ABI definitions.
- [D3D11 queries](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query) provide event completion and timestamp/disjoint measurements; [GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata) distinguishes ready from pending results.
- [Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api) describes the output-oriented alternative.

Microsoft's [HWND capture sample at ee50e2e](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/tree/ee50e2ea137dcef7b82ba504eff7435e5ebf5294/cpp/ScreenCaptureforHWND) was inspected for interop/lifecycle patterns. It uses the dispatcher-bound Create path; this prototype implements its own background ownership design. Context7's WGC discovery returned a Rust/Python wrapper, which does not establish native SDK signatures; the official pages and SDK header above resolve that gap.

## Ownership decision

One MTA worker owns frame-pool operations and all immediate-context calls. FrameArrived only signals the worker; it does not touch GPU resources. At most four pool frames and three owned textures exist. On each wake, drain at most four frames and keep the newest; count discarded received frames. If every destination slot is busy, discard new work rather than reuse an in-flight texture.

Each occupied slot owns its WinRT source-frame lease until its GPU event query completes. A COM pointer alone does not guarantee immutable frame content. Timestamp queries measure the copy; an event query governs reuse. Flush submits headless commands without waiting. A condition-variable wait (2 ms while work is in flight) permits completion polling even if the pool has stopped delivering new frames. No per-frame blocking GPU wait is used.

The worker drains in-flight copies before a resize, releases old leases, recreates the pool and allocates a new ring generation. Capture closure, HWND/process identity change, monitor move, source staleness and device errors stop the session; restart is explicit. Stop revokes events, ends capture, drains submitted work with a bounded timeout, closes the pool and releases resources. No descriptor is exposed to another GPU consumer in M1.

## Color and metrics limitations

The first path is SDR BGRA8, alpha ignored. It performs a valid-content GPU copy without CPU mapping, normalization or a tone mapper. HDR is outside the acceptance scope. Capture FPS counts drained WGC frames, not distinct game presents. Dequeue age and observed GPU-ready age use matched source timestamps. The latter includes polling delay; GPU copy duration comes from timestamp queries. Missing/disjoint GPU results are unavailable, never zero.

Black-content detection and a texture preview remain follow-up work: freshness alone cannot prove valid pixels. Raw trace capture is bounded and opt-in. Acceptance still requires an actual live run, including separate resize/minimize/restore and game-exit checks. See the [M1 status](../milestones/milestone-1.md) and [runbook](../capture-runbook.md).
