# Capture freshness and full-quality pixels

Audit base: `fa82502a1732dea6bcb3f4c31e1b0ba8f4e47526`. The reported symptom was approximately 59 capture FPS while F8 rejected a source older than 3000 ms. The screenshot is not a matched frame trace, so it cannot establish which machine-side delay produced that instance. This change fixes concrete code paths that could withhold ready images or misrepresent delivery; it does not claim an unobserved Sekiro/RTX run succeeded.

## Findings and corrections

1. **Clock conversion was already fundamentally correct.** WGC `SystemRelativeTime` represents the compositor's QPC time as a TimeSpan. It is converted from 100 ns units to milliseconds, while raw QPC uses the actual `QueryPerformanceFrequency`. No startup offset, wall clock, `GetTickCount64` offset or replacement with the current time is applied. Invalid, future, duplicate and regressing source times cannot advance healthy source cadence. [WGC timestamp definition](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.systemrelativetime?view=winrt-26100), [QPC conversion and clocks](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps).
2. **GPU profiling was blocking pixel delivery.** The previous completion loop waited for start/end/disjoint timing queries after the event already confirmed completion. Timing now gets one nonblocking observation; unavailable/unsupported profiling remains unavailable. Only the actual completion event and nonblocking staging map control pixel readiness. Device removal still faults. [D3D11 completion and timestamp queries](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query), [GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata).
3. **Ring slot order was mistaken for delivery order.** A reused slot 0 can contain a newer source than slots 1/2. Capture now chooses one newest completed, usable source and rejects late older completions. It releases the WGC frame lease immediately after the GPU completion event, instead of retaining a pool buffer through profiling and CPU work.
4. **FPS and ages described different stages.** Source FPS previously counted every dequeue, including invalid timestamps. GPU ready age stopped before CPU conversion and the sink callback. The snapshot and trace now distinguish increasing compositor source arrivals, GPU completion, and delivered consumer frames. A new callback does not make old source pixels fresh.
5. **Shutdown/resize drains were still calling the sink.** An invalidated generation now drains ownership without publishing its old images.

`fresh_at` uses the existing strict source age budget: age must be finite, nonnegative and **less than 120 ms**. Capture checks this before live readback and again after CPU pixel preparation. An image aged during GPU/CPU work is dropped. Existing InputEngine checks still apply independently at arming and dispatch. The GPU's two-second fault timeout is resource recovery, not permission to use a two-second-old input frame.

## AI pixels and preview are separate

The bounded capture ring owns the native SDR BGRA8 texture and its native-size staging image. `SmallFrame.color` is now a bit-preserving native-resolution readback of that owned source. AI crop/resize and shared native replay preprocessing consume these pixels. The image is not resized to the debug preview and enlarged again.

The GPU produces a separate aspect-preserving image capped at 640 × 360, never upscaled, in `SmallFrame.preview_color`. UI and the optional recording ring use it. If the optional preview is not immediately mappable, it is omitted; AI does not wait for it. The shared `FramePixels.h` grayscale derivative always uses full native pixels and never rewrites sequence, generation or source time. Recording metadata retains original source dimensions and its actual encoded preview dimensions.

No unowned D3D pointer escapes the ring. Source frames remain capped at 8,294,400 pixels and 8192 pixels on either axis. There are three owned slots and four WGC pool buffers; the downstream live queue remains one frame. The ring reuses a slot only after the GPU event completes. Preview and native CPU images use immutable shared ownership, so publishing a snapshot does not copy the entire image while holding the consumer lock.

This is an explicit full-quality **GPU → CPU → GPU bridge** for current ORT, not GPU tensor interoperability. Nominal tightly packed payload at 60 delivered frames/second would be about 553 MB/s for 1920 × 1080 plus preview, or 2.05 GB/s for 3840 × 2160 plus preview. These are byte-count estimates, not measured PCIe/driver throughput or measured game performance. At 4K, the three owned native textures, native staging textures, preview targets and preview staging textures total about 205 MB of resource payload; WGC pool and inference allocations are additional, and driver memory placement varies. `readback_bytes` counts actual CPU row-copy payload, including frames that age out during preparation; driver row padding is excluded.

Direct D3D/ORT device binding remains a separate optimization that needs adapter-matched consumer ownership, tensor layout and completion signaling. A D3D texture pointer is not an ONNX tensor.

## Read the evidence correctly

| Snapshot/trace value | Meaning |
|---|---|
| `capture_fps` | Increasing valid compositor source frames dequeued over the latest two seconds |
| `copy_fps` | Observed GPU completion events over the latest two seconds |
| `delivered_fps` | Frames whose native pixels reached and returned from the sink callback |
| `source_age_ms` | Current QPC minus latest valid compositor source time |
| `delivered_age_ms` | Current QPC minus source time of latest sink-delivered frame |
| `ready_age` | First GPU event observation minus the same frame's source time |
| `consumer_age` | Sink return minus that frame's unchanged source time |
| `sink_time` | Sink callback return minus native/optional-preview preparation completion |
| `readback_bytes` | Native + optional preview BGRA bytes actually copied to CPU rows |
| `delivery_detail` | Current source/consumer freshness reason, independent of displayed FPS |

“Delivered to sink” means the callback received that frame. It does not establish that a model produced a prediction or InputEngine accepted a Dodge.

Trace schema 2 adds raw WGC TimeSpan ticks, native consumer-ready QPC, sink-return QPC, sink duration, readback bytes and preview dimensions to the existing source/dequeue/GPU fields. Metadata records QPC frequency, the unmodified age limit and pixel contracts. `tools/analyze_capture_trace.py` reads v1 and v2, counts GPU completions even if their CPU delivery is superseded, and reports matched source-to-consumer ages. Missing v1 consumer measurements remain unknown. Each FPS uses events from its own stage inside the requested QPC window.

## Verification

- `Tests/CaptureTimingTests.cpp`: long-uptime WGC/QPC conversion at 3.125, 10, 24 and 3000 MHz; sub-millisecond precision; unchanged strict age boundary; NaN/future/3001 ms rejection; ring wrap/newest completion/late completion; generation and source ordering; frame aging during preparation; padded BGRA rows and immutable source metadata. Compiled and passed locally with C++20 and warnings enabled.
- `Tests/capture_trace.py`: four executable analyzer regressions, including 60 dequeues/second with frozen source time, separate GPU/consumer completion, future source plus out-of-order delivery, and legacy traces without fabricated consumer measurements. Passed locally.
- Existing Windows `gpu_readback_functional` test now verifies bit-exact full native BGRA including one-pixel details, separate color preview dimensions/channels, grayscale, aspect ratio, padding and shared ownership for five geometries. The existing Windows `sample_recording_functional` test now supplies a 1920 × 1080 native frame plus 64 × 36 preview and verifies the encoded preview roundtrip. These require the Windows CI run for this revision; local Linux checks are not a substitute for D3D11/WIC execution.

No live Sekiro source, target RTX 3070, gameplay latency measurement or machine-specific reproduction is available in this development environment. Source/capture correctness checks do not establish attack model accuracy or successful Dodge timing.
