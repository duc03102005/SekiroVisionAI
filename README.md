# SekiroVisionAI

Windows native, offline single-player Auto Dodge for Sekiro: Shadows Die Twice, targeting an NVIDIA RTX 3070.

**Current branch: `feature/temporal-ai-pipeline` (0.3).** The existing native app now has clear color capture/preview, a causal RGB ROI history, ONNX Runtime DirectML/CPU inference, model threat/TTI decisions, real Shift or direction+Shift, and opt-in training sample recording. The earlier CV heuristic remains an explicitly selected fallback. Auto Dodge starts OFF; **F8** toggles in game, **F9** disables/releases, **F10** tests input when enabled, **F7** toggles recording, and **F4/F5/F6** mark samples for review.

**No gameplay-trained AI model is bundled yet.** The 60-source catalog covers all requested boss families as research links; acquired/reviewed gameplay remains 0. Three trainable temporal architectures, clip mining, annotation, supervised/self-supervised training and ONNX export are implemented. Synthetic learning/parity tests are labeled separately and cannot enable model Auto Dodge. This version has not demonstrated reliable model-driven Dodge in the user's Sekiro game.

The user's 2026-09-10 instructions removed milestone PASS/FAIL development gates and requested parallel dataset/model/capture work. All eight reviewed Agent Skills are preserved. Dataset completion and formal capture benchmarking do not block implementation or early training on a small authorized reviewed set.

- [Run this Windows version — Vietnamese guide](docs/TEMPORAL_QUICKSTART_VI.md)
- [Temporal scope: implemented work and actual limitations](docs/temporal-ai-scope.md)
- [Executed validation and Windows package](docs/temporal-validation.md)
- [Dataset tools and annotation](DatasetTools/README.md)
- [Three temporal baselines, training and ONNX export](Training/README.md)
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
