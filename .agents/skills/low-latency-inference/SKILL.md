---
name: low-latency-inference
description: Profile and optimize SekiroVisionAI batch-one GPU inference on Windows RTX 3070 using ONNX Runtime, CUDA or TensorRT with explicit transfer, stream and end-to-end latency accounting.
---

# Low-latency inference

Use [benchmark-contract.md](references/benchmark-contract.md) before performance claims. This skill specifies experiments; M0 does not authorize runtime implementation or model training.

## Establish a compatible baseline

Compare ONNX Runtime CUDA EP, TensorRT EP and TensorRT for RTX against the same exported model, preprocessing and evaluation data. Check selected Windows x64 packages, GPU/driver support, CUDA/cuDNN versions, provider availability and supported operators. Do not choose “latest” from memory or combine a main-branch design document with a released package API.

At the 2026-09-10 review, ORT's TensorRT RTX documentation marks the built-in EP deprecated and directs new work to a standalone EP ABI plugin. Verify that integration path and the selected release before copying examples; package availability and plugin registration differ. RTX 30-series support in the documentation is eligibility, not a measured speed guarantee for the 3070.

Begin with a supported prebuilt runtime unless source changes are needed. The imported `ort-build` skill is only for compiling ONNX Runtime itself in its own pinned source checkout. It is not an instruction to build that large dependency during skill setup.

Run with batch B=1 and continuous live game load. Temporal sequence length T=8/16/24/32 is not throughput batching. Compare 320×320 and 416×416 for shortlisted models. Record model/engine hash, tensor layout, precision and preprocessing version.

## Make transfers explicit

- Inventory each resource: WGC/D3D texture, CUDA mapped array or shared resource, contiguous input tensor, hidden-state tensors and compact CPU decision output.
- Prove D3D/CUDA adapter compatibility. Use documented interop; registration may reject capture textures. An owned GPU texture plus GPU copy/preprocessing may be the correct baseline.
- ONNX I/O Binding can avoid implicit host transfers only if memory location, allocation lifetime and synchronization are correct. Binding a CPU pointer with a CUDA label is invalid. Do not treat an `ID3D11Texture2D*` as an OrtValue data pointer.
- Keep crop/resize/color conversion/normalization on GPU when supported. Fuse operations only after checking exact numerical parity with training.
- Reuse device allocations, sessions and fixed-size buffers. Retain resources until their last GPU consumer completes. Copy small decision outputs to CPU when appropriate and measure that synchronization/readback.
- Pinned memory optimizes an unavoidable host transfer; it does not make it zero-copy. Distinguish no host readback from no device-to-device copy.

## Streams, graphs and precision

Define a bounded asynchronous schedule and stream/event ownership. Avoid concurrent reuse of one input buffer or recurrent state. Measure queue age and GPU contention with the game. A queue that maximizes GPU utilization but delays decisions is a failure for this task.

Warm up each model/provider/shape before measurement; separate session creation, engine building, graph capture and first-run allocation from steady-state latency. Report those startup costs separately.

Test FP16 against an FP32 reference on event/TTI outputs and full decision traces, including threshold-edge examples. Small score changes can create unwanted Dodge. INT8 requires representative calibration and quality validation before consideration.

CUDA Graph is an optional optimization: verify the selected provider's rules for shapes, addresses, supported operations, streams, control flow and concurrency. Keep capture/replay allocations stable, account for recurrent buffers and use separate validated graphs or recapture when shapes change. Do not assume constraints of the legacy CUDA EP and the newer plugin EP are identical.

For TensorRT, report graph partitioning, unsupported-op fallbacks, engine-cache invalidation and build duration. CUDA or CPU fallback can add transfers and jitter. Runtime provider selection in the UI must reflect the provider actually executing the graph.

## Profile and decide

Measure capture, preprocessing, detection, temporal inference, threat decision and input submission separately and as paired end-to-end traces. Use CUDA events for GPU work and monotonic CPU timers for CPU stages with explicit clock correlation. CPU enqueue duration is not GPU inference duration.

Report p50/p95/p99 and maxima, frame age, skipped/dropped frames, memory/VRAM, game FPS delta and decision quality. Nsight Systems/NVTX may help explain contention, but profiler overhead needs a matched unprofiled run. CPU/GPU usage without a performance counter is unavailable, not 0%.

Select the smallest configuration meeting event/false-Dodge criteria under live load. Do not promise the UI example's 6.2 ms or 10.5 ms total before measuring on the RTX 3070.

## Official references

- [ORT CUDA EP](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html)
- [ORT I/O Binding](https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html)
- [ORT TensorRT EP](https://onnxruntime.ai/docs/execution-providers/TensorRT-ExecutionProvider.html)
- [ORT TensorRT RTX EP](https://onnxruntime.ai/docs/execution-providers/TensorRTRTX-ExecutionProvider.html)
- [CUDA D3D11 interop](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__D3D11.html)
