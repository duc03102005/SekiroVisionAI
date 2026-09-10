# SekiroVisionAI

Windows native, offline single-player Auto Dodge MVP for Sekiro: Shadows Die Twice, targeting an NVIDIA RTX 3070.

**Current branch: `feature/auto-dodge-mvp`.** The application now connects WGC capture → a user-selected combat ROI → camera-compensated frame differences/sparse flow → temporal threat arming → real Shift or direction + Shift through Windows SendInput. Auto Dodge starts OFF. **F8** toggles in-game, **F9** disables/releases, and **F10** performs one manual input test while enabled. This provisional CV detector is not a trained attack model; live game accuracy has not been measured.

The user's 2026-09-10 instruction removed all milestone PASS/FAIL development gates. The eight Agent Skills are preserved as technical references. Dataset completion, formal benchmarking and trained models do not block work on this end-to-end MVP.

- [Run the Auto Dodge MVP — Vietnamese guide](docs/MVP_QUICKSTART_VI.md)
- [MVP architecture, detector and input behavior](docs/mvp-architecture.md)
- [Current user scope; no milestone gates](docs/mvp-scope.md)
- [Historical capture probe runbook](docs/capture-runbook.md)
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

The validator is standard-library-only. It validates the intentionally simple project frontmatter format, local references, review file hashes and the annotation example. It does not gate application work on milestone status or certify detector/game performance.

The source of truth is [duc03102005/SekiroVisionAI](https://github.com/duc03102005/SekiroVisionAI). The reviewed skill tree was published as [15927a0](https://github.com/duc03102005/SekiroVisionAI/commit/15927a071fe415eda7920e118d954c0c8a5e864a) with `chore: initialize SekiroVisionAI agent skills`, preserving the user's initial README commit.
