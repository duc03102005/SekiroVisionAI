# SekiroVisionAI

Windows native, offline single-player research project for vision-led automatic Dodge in Sekiro: Shadows Die Twice, targeting an NVIDIA RTX 3070.

**Current milestone: M0 — Agent Skills. Overall status: FAIL / blocked on GitHub repository creation and publication.** This checkout contains reviewed skills and setup documentation only. No capture engine, model, trained dataset or input automation has been implemented.

- [Milestone 0 evidence and remaining step](docs/milestones/milestone-0.md)
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

The intended source of truth is `duc03102005/SekiroVisionAI`. A local commit is prepared with `chore: initialize SekiroVisionAI agent skills`; it must still be published and verified. Do not use a local backup as evidence of a GitHub push.
