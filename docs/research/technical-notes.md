# Technical grounding for Agent Skills — 2026-09-10

This document preserves research from the original M0 session. Its initial GitHub 404 blocker was subsequently resolved; see the [M0 PASS record](../milestones/milestone-0.md). Runtime language, SDK/provider versions, temporal model and overlay decisions belong to the subsequent architecture records.

## Connectivity and source handling

GitHub get-profile succeeded for `duc03102005`. Listing accessible repositories succeeded; none of the returned repositories is SekiroVisionAI. Direct lookup of `duc03102005/SekiroVisionAI` returned HTTP 404. The exposed connector has repository read, content, Git-tree/commit and ref-update tools, but no repository-creation tool. Git is present locally; GitHub CLI is absent. Existing unrelated repositories were not changed.

Context7 resolve/query succeeded for `/microsoft/onnxruntime`. The Windows SDK query returned Windows App SDK as well as Python bindings; `/microsoft/windowsappsdk` was chosen for a follow-up query. The Windows results included design/specification snippets and did not settle the required WGC/native-interop API contract. Official Microsoft API pages were therefore also read. No version was selected merely from a Context7 search ranking.

Initial general web searches returned no results; direct official pages and pinned GitHub source retrieval worked. The direct CreateFreeThreaded API page could not be opened in this environment. Its precise selected-SDK signature/version remains an explicit implementation-time check rather than a claimed verified build.

## Findings that changed the skills

WGC offers desktop HWND capture through CreateForWindow, with its documented Windows 10 1903 requirement. Frame-pool recreation is the documented route for device/size changes; this supports explicit generation/lifetime handling. [CreateForWindow](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow), [WGC lifecycle](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture).

Desktop Duplication returns desktop updates in a DXGI surface and has rotation/cursor/dirty-region handling. It is a comparison option for output capture, not a drop-in HWND identity contract. [Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api).

CUDA D3D11 interop uses documented resource registration/mapping. ORT I/O Binding governs device tensors and avoids implicit host transfers only when the actual memory and lifetime are correct. The project therefore requires an explicit texture-to-tensor contract and measured synchronization. [CUDA D3D11 interop](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__D3D11.html), [ORT I/O Binding](https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html).

The reviewed TensorRT RTX page marks the built-in ORT EP deprecated and recommends the standalone EP ABI plugin. It lists Ampere/RTX 30-series and later support. Hardware eligibility does not establish 3070 latency; plugin/release compatibility remains to be tested. [TensorRT RTX EP](https://onnxruntime.ai/docs/execution-providers/TensorRTRTX-ExecutionProvider.html).

The ORT CUDA provider documentation and Context7 results cover different released/plugin development paths. The skill requires a consistent selected version, documented stream/graph constraints and measured CPU/GPU fallbacks. [CUDA EP](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html).

MMAction2 is a video-understanding research toolbox; VideoMAE provides video pretraining code; the TCN paper motivates a recurrent/convolutional comparison. None proves Sekiro TTI accuracy or a causal RTX 3070 runtime. The custom skill treats them as candidates requiring domain data, causal adaptation and export/latency checks. [MMAction2](https://github.com/open-mmlab/mmaction2), [VideoMAE](https://github.com/MCG-NJU/VideoMAE), [TCN paper](https://arxiv.org/abs/1803.01271).

SendInput reports submitted events, has integrity-level restrictions and can interact with keys already held. The future input engine must distinguish submission from actual game response and account for physical key state and failure paths. [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).

## Unmeasured and intentionally deferred

No Windows host or running Sekiro/RTX 3070 was used in this task. Capture FPS, GPU/CPU load, TTI error, attack precision, false Dodge rate and end-to-end latency are unmeasured. No YouTube media was downloaded, no training was run and no game input was sent. No CUDA/cuDNN/ONNX/TensorRT package was installed.

The capture 10-minute protocol and proposed drop/stall tolerances are project test proposals that make the user's stability criterion reviewable. They are not claims about SDK guarantees or completed benchmark results.
