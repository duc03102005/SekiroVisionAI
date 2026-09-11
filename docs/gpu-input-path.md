# Current GPU input path and measured scope

The application owns three full-resolution WGC/D3D11 source textures. AI receives native-resolution BGRA pixels; the independently resized color preview is limited to 640×360 and is used only for UI/recording. Both carry the original frame's sequence and WGC/QPC time. GPU completion and source freshness determine delivery; optional GPU profiling queries cannot hold an otherwise ready frame.

The current ONNX path still reads pixels to CPU, crops/resizes/normalizes there and supplies an ordinary CPU tensor to ORT. A GPU execution provider transfers that input as required. This is **not zero-copy**, and native-resolution readback can cost substantial bandwidth. The UI reports actual delivery age and the provider(s) observed during model warmup. A selected provider is not evidence of an optimal RTX 3070 configuration.

## Interop decision

The pinned ORT DirectML 1.24.4 header exposes `OrtDmlApi::SessionOptionsAppendExecutionProvider_DML1`, `CreateGPUAllocationFromD3DResource` and `FreeGPUAllocation`. These APIs accept a D3D12 device/queue/resource. They do not accept a WGC `ID3D11Texture2D` as an ONNX tensor. Correct integration requires a matching adapter, an owned shareable resource, explicit copy/compute synchronization, GPU RGB/NCHW preprocessing, persistent tensor buffers and a lease held until inference completion.

CUDA exposes D3D11 device/resource interop, but the texture format, adapter, registration, map/unmap ordering and buffer lifetime must be supported and verified. A source texture is not inherently linear tensor memory. Shipping a source-texture pointer labeled as CUDA memory would be invalid.

The safe full-quality bridge is implemented now. GPU preprocessing and device tensors remain unimplemented, rather than simulated or advertised as complete. Their acceptance requires native preprocessing parity and paired source-to-decision traces on the chosen GPU path. Hosted CPU/WARP tests do not measure the RTX 3070 while Sekiro renders.

Primary references reviewed 2026-09-10: [ORT DirectML provider and device constraints](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html), [ORT I/O binding](https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html), [NVIDIA D3D11 interop API](https://docs.nvidia.com/cuda/cuda-runtime-api/group__CUDART__D3D11.html). Context7 lookup for `/microsoft/onnxruntime/v1.25.0` corroborated DML1 device/queue ownership; implementation checks use the actual packaged 1.24.4 headers.
