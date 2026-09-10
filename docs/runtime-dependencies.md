# Native runtime dependencies and source evidence

The default Windows package uses Microsoft.ML.OnnxRuntime.DirectML1.24.4
(commit2d924974ef147392ced8409d36bd6d2e7fcc8a74) and its declared native dependency
Microsoft.AI.DirectML1.15.4. Source NuGet archives and SHA256 values are pinned in
`cmake/OnnxRuntime.cmake`. Only Windows x64 release DLLs are packaged, with their
license/third-party notices. C++ app-local VC143 CRT files come from the build
toolchain's redistributable directory; these remain Microsoft components.

DirectML is in sustained engineering; it is used here to keep the existing
unpackaged native app runnable without Python/CUDA installation. CPU is explicitly
selectable. The selected provider string reports CPU fallback as possible;
registering DirectML does not prove every node ran on GPU. No RTX3070 latency
or game-content accuracy is claimed from CI. CUDA is a separate source-build
flavor using official ORT1.25.0 CUDA12 assets; its external CUDA/cuDNN dependencies
are not bundled. TensorRT/FP16/graphs are not implemented or benchmarked yet.

Official sources checked2026-09-10:

- [ONNX Runtime install](https://onnxruntime.ai/docs/install/): native packages,
  VC++ runtime requirements, CUDA/cuDNN setup.
- [DirectML execution provider](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html):
  DirectX12 devices, sequential session and disabled memory pattern requirements.
- [NuGet native ORT DirectML1.24.4](https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime.DirectML/1.24.4):
  exact release and declared DirectML1.15.4 dependency (also read from nuspec).
- [Microsoft.AI.DirectML1.15.4](https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4).
- [Official ORT1.25.0 release](https://github.com/microsoft/onnxruntime/releases/tag/v1.25.0):
  CPU/CUDA archives and published SHA256 digests.
- [VC++ redistribution](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170):
  app-local redistributable runtime files.
- [Context7-backed ORT C++ API source](https://github.com/microsoft/onnxruntime/blob/v1.25.0/include/onnxruntime/core/session/onnxruntime_cxx_api.h):
  Session/metadata/named tensors, checked against downloaded package headers.

Color capture currently reads back up to1280×720 BGRA, then CPU ROI preprocessing
creates an RGB float tensor. DirectML/CUDA may upload that CPU tensor. This is an
explicit implementation tradeoff, **not zero-copy GPU-to-model integration**.
