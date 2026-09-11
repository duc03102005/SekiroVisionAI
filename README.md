# SekiroVisionAI

Windows native, offline single-player Auto Dodge for Sekiro: Shadows Die Twice, targeting an NVIDIA RTX 3070.

**Current branch: `feature/complete-application` (0.4 development).** The native Windows application builds as `SekiroVisionAI.exe`. It automatically discovers Sekiro, captures full-resolution color frames independently of its smaller preview, and uses a shared native combat pipeline for live capture and prerecorded-video replay. The pipeline connects automatic actor detection/tracking, temporal ONNX inference, threat episodes, directional selection and real Windows input. **F8** toggles Auto Dodge; **F9** cancels and releases owned keys. Manual ROI, heuristic mode, model selection and recording controls are in Advanced/Debug.

**The complete Auto Dodge release is not ready.** Eight actual videos, about 40 minutes, have been acquired with hashes, original timestamps and documented usage. Motion representation, attack-phase and Wolf/enemy models have been trained on real reviewed frames. They remain development candidates: the role and phase models transfer poorly to Gyoubu, and reviewed exact contact/TTI labels remain unavailable. No production model is bundled. The internal Windows package therefore cannot provide the requested normal-mode model-driven Dodge. Synthetic test models never enable it.

Development continues without milestone approval gates. All eight reviewed Agent Skills are preserved. Missing data and RTX 3070 measurements do not block implementation; they remain limits on claims of all-boss accuracy, timing and live-game success.

- [Current implementation and evidence](docs/complete-application-status.md)
- [Windows application guide](docs/APPLICATION_README_VI.txt)
- [Shared native ReplayHarness](ReplayHarness/README.md)
- [Actual gameplay learning experiments and reproducible commands](Training/REAL_GAMEPLAY_EXPERIMENTS.md)
- [Actor detector training, native contract and generalization failures](Training/targets/README.md)
- [Full-quality capture and the remaining GPU transfer work](docs/gpu-input-path.md)

- [Run this Windows version — Vietnamese guide](docs/TEMPORAL_QUICKSTART_VI.md)
- [Temporal scope: implemented work and actual limitations](docs/temporal-ai-scope.md)
- [Executed validation and Windows package](docs/temporal-validation.md)
- [Dataset tools and annotation](DatasetTools/README.md)
- [Temporal architectures, training and ONNX export](Training/README.md)
- [Video source research and acquisition evidence](Dataset/catalog/source_research.md)
- [Training sample recording format](docs/recording-format.md)
- [Pinned native dependencies](docs/runtime-dependencies.md)
- [Previous Auto Dodge MVP guide](docs/MVP_QUICKSTART_VI.md)
- [MVP architecture, detector and input behavior](docs/mvp-architecture.md)
- [Current user scope; no milestone gates](docs/mvp-scope.md)
- [Historical capture probe runbook](docs/capture-runbook.md)
- [Historical capture evidence and unmeasured hardware results](docs/milestones/milestone-1.md)
- [Runtime decision](docs/adr/001-native-runtime.md) and [capture decision](docs/adr/002-window-capture.md)
- [Milestone 0 evidence](docs/milestones/milestone-0.md)
- [External skills review and provenance](docs/research/external-skills-review.md)
- [Custom skills review](docs/research/custom-skills-review.md)
- [Current-source research notes](docs/research/technical-notes.md)
- [Original project brief](docs/project-brief.md)
- [Agent instructions](AGENTS.md)

Six project-specific skills live under `.agents/skills/`, plus adapted MIT-licensed Microsoft `winui-code-review` and `ort-build` skills. Those two are conditional tools: WinUI frontend review if WinUI is selected, and building ONNX Runtime only if a source build is justified. Their scripts, packages and binaries are not installed.

Validate this setup from the repository root:

```text
python tools/validate_skills.py
```

The validator is standard-library-only. It validates the intentionally simple project frontmatter format, local references, review file hashes and the annotation example. It does not gate application work on milestone status or certify detector/game performance.

The source of truth is [duc03102005/SekiroVisionAI](https://github.com/duc03102005/SekiroVisionAI). The reviewed skill tree was published as [15927a0](https://github.com/duc03102005/SekiroVisionAI/commit/15927a071fe415eda7920e118d954c0c8a5e864a) with `chore: initialize SekiroVisionAI agent skills`, preserving the user's initial README commit.
