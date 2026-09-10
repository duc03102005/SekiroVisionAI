# SekiroVisionAI

Windows native, offline single-player research project for vision-led automatic Dodge in Sekiro: Shadows Die Twice, targeting an NVIDIA RTX 3070.

**Milestone 0 — Agent Skills: PASS.** The reviewed setup is published and verified on GitHub. **Milestone 1 — Capture Engine: in progress.** A native Windows capture probe is implemented on `feature/capture-engine`; live Sekiro/RTX 3070 performance is not measured.

- [Build/run the Windows capture probe and measure M1](docs/capture-runbook.md)
- [Milestone 1 evidence and pending hardware gate](docs/milestones/milestone-1.md)
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

The validator is standard-library-only. It validates the intentionally simple project frontmatter format, local references, review file hashes, the annotation example and the M0 application-code boundary. It does not prove skill behavior, certify absence of malicious content or pass any game-performance milestone.

The source of truth is [duc03102005/SekiroVisionAI](https://github.com/duc03102005/SekiroVisionAI). The reviewed skill tree was published as [15927a0](https://github.com/duc03102005/SekiroVisionAI/commit/15927a071fe415eda7920e118d954c0c8a5e864a) with `chore: initialize SekiroVisionAI agent skills`, preserving the user's initial README commit.
