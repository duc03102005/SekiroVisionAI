---
name: windows-gpu-capture
description: Design and validate SekiroVisionAI Windows HWND capture with Windows.Graphics.Capture, D3D11 textures, bounded frame ownership, resize recovery and capture latency measurement.
---

# Windows GPU capture

Read the project milestone gate in `AGENTS.md` before implementation. Capture work requires M0 PASS. Use [frame-contract.md](references/frame-contract.md) for the ownership and measurement contract.

## Establish the capture path

1. Record Windows build, SDK/toolchain, display mode/refresh rate, HDR, game presentation mode, source FPS and adapter LUID. Check capture support at runtime.
2. Start the comparison with Windows.Graphics.Capture (WGC), an explicit user-selected Sekiro HWND and D3D11. Verify the window's process identity; handle process exit and HWND reuse. Use the documented `IGraphicsCaptureItemInterop::CreateForWindow` desktop interop, not a guessed factory overload.
3. Compare Desktop Duplication if output-level capture is needed or WGC fails on the measured configuration. Duplication captures an output; cropping is not equivalent to tracking a window and requires occlusion, DPI, rotation, cursor and monitor handling. Record the decision after M0 in ADR-002.
4. Use an `ID3D11Device` and a compatible WinRT `IDirect3DDevice`. Retrieve `ID3D11Texture2D` through the documented surface interop. Check HRESULTs, format, content size, device identity and texture flags.
5. Evaluate `Direct3D11CaptureFramePool.CreateFreeThreaded` for background frame delivery. Verify its SDK signature and threading behavior in current official docs. Keep handlers short; do not infer that a free-threaded event makes the D3D11 immediate context thread-safe. Serialize context access or use an explicitly supported synchronization design.

WGC → D3D texture → GPU crop/resize/color conversion → inference is the preferred candidate pipeline. D3D12 is an option only when a measured benefit or an interop requirement justifies its additional synchronization and resource-state complexity.

## Own every frame

- Retain a frame lease until all reads using its surface have a safe lifetime, or enqueue a GPU copy into an application-owned bounded texture ring while the source is valid. Releasing the frame and retaining a COM texture pointer alone is not a content-immutability guarantee.
- Keep only the newest useful pending frame. Bound ring slots and GPU work in flight. If no safe slot exists, drop work and count it; never overwrite a resource still used by the GPU.
- Specify producer/consumer ownership, command submission order, GPU completion evidence and shutdown ordering. Avoid per-frame global waits or busy polling. Pair D3D query/fence mechanisms with the actual API/device capabilities.
- Use GPU crop and normalization. Do not map a full staging texture to CPU by default. A GPU-to-GPU copy is acceptable when needed for ownership/interop; call it a copy, not zero-copy.
- For CUDA, validate adapter matching and resource registration compatibility. WGC textures may need an application-owned interoperable texture. A mapped D3D texture can be a CUDA array, while an ONNX input usually requires a contiguous tensor buffer. Track the conversion/copy and synchronization explicitly.

## Lifecycle and presentation modes

- Compare `ContentSize` with allocated texture size. On resize, invalidate old coordinate transforms, drain or retire in-flight leases safely, and use frame-pool `Recreate` with the new dimensions. Ignore invalid/zero sizes while minimized.
- Handle capture item Closed, device removal/reset, game restart, DPI changes, monitor moves and adapter changes. Suspend downstream predictions on discontinuity; require fresh temporal history after recovery.
- Test windowed and borderless first; test exclusive fullscreen separately. Do not guarantee any capture path will work for every presentation mode. Black/stale frames are faults, not valid empty scenes.
- Define BGRA/RGBA order, alpha, color space, HDR tone mapping and SDR conversion. Training and live preprocessing must match.
- Overlay must not contaminate model input. Prefer window-target capture of the game and verify the actual result with overlay toggled.

## Measure M1 honestly

Use monotonic CPU timestamps, WGC frame timestamps where available and GPU timestamp queries with their frequency/disjoint handling. Distinguish callback delay, GPU processing duration, queue wait and frame age. Do not subtract clocks with different epochs or equate CPU command submission time with GPU completion.

Report delivered FPS, distinct source-frame cadence where observable, frame intervals p50/p95/p99, intentional drops, callback gaps, CPU/GPU usage, bytes copied, memory growth and capture-to-consumer age. A display running at 120 Hz does not prove the game supplies 120 unique frames per second. Temporal sampling uses actual timestamps, including gaps.

Use the proposed acceptance protocol in the reference, record deviations before a run and retain raw evidence. A Linux build, a mock source or a counter displaying 60 is not a Sekiro/Windows performance PASS.

## Official references

- [WGC overview and lifecycle](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture)
- [CreateForWindow requirements](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [CUDA D3D11 interop](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__D3D11.html)

Consult Context7 for discovery and check actual SDK headers/official API pages for the selected version. Search results about Python Windows bindings do not establish native API signatures.
