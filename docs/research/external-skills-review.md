# External Agent Skills review — 2026-09-10

## Disposition

| Source | Reviewed scope | Decision and reason |
|---|---|---|
| microsoft/win-dev-skills: winui-code-review | Entire SKILL.md, its sole quality-rules.md reference, MIT LICENSE, and subtree inventory | Import adapted Markdown for a future WinUI frontend review. No UI framework selection is implied. |
| microsoft/onnxruntime: ort-build | Entire SKILL.md, MIT LICENSE, and directory inventory (no bundled scripts) | Import with a project-scope preface. Use only if compiling ORT itself becomes necessary; do not run upstream build commands in this repo. |
| NVIDIA/skills: deepstream-profile-pipeline | Entire entrypoint and repository inventory; references were not imported or fully audited | Do not import: Ubuntu/container DeepStream workflow, multi-stream throughput batching and automatic pipeline presets do not match the native Windows, one-game, batch=1 requirement. |

Primary sources are pinned:

- [Microsoft WinUI skill](https://github.com/microsoft/win-dev-skills/blob/f94bab12f67f8670567c08b079dfd3179a1d15b4/plugins/winui/agent-plugin/skills/winui-code-review/SKILL.md), commit `f94bab12f67f8670567c08b079dfd3179a1d15b4`.
- [ONNX Runtime ort-build](https://github.com/microsoft/onnxruntime/blob/f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7/.github/skills/ort-build/SKILL.md), commit `f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7`.
- [NVIDIA profiling skill](https://github.com/NVIDIA/skills/blob/be3d1a48f91de1abd09536f9a73d182dbecfb337/skills/deepstream-profile-pipeline/SKILL.md), commit `be3d1a48f91de1abd09536f9a73d182dbecfb337`.

The exact upstream/installed hashes and local paths are in [external-skills.lock.json](../../.agents/external-skills.lock.json). The original WinUI skill, reference and LICENSE Git blob hashes match the upstream tree. The ORT skill blob matches the upstream directory listing. LICENSE bytes were retrieved from the same pinned revisions and retained beside the skills.

## What was inspected

- Read both imported entrypoints and all their bundled references in full before import. Inspected directory inventories for scripts, binaries, package manifests and links. No executable files are included in the imported skill directories.
- Reviewed shell snippets and suggested operations: ORT build/update/test commands, output redirection, Python build tooling, and WinUI analyzer/package instructions. These can build code, fetch dependencies or run processes if later invoked. They were not executed during M0.
- The ORT build driver, build.bat/build.sh, upstream AGENTS.md and optional sibling skills are not bundled. They have not been audited by this import review. Before a future source build, review those resources at the actual source revision and record dependency/network behavior.
- No credential collection, encoded downloader, persistence installer, exfiltration destination, instruction to override user/system scope, or automatic executable hook was found in the installed copies. This is a bounded manual review, not a guarantee about future upstream changes.
- WinUI's security reference contains a clearly labeled bad Process.Start example. It remains explanatory text, is not executed, and is not an installation command.
- Installation consisted of copying reviewed Markdown/LICENSE files and making the changes below. There was no execution of remote scripts or installation of upstream plugins, MSBuild hooks, analyzers or model runtimes.

## Adaptations after review

### winui-code-review

1. Restricted triggering to a selected WinUI frontend.
2. Removed the assumption that upstream BuildAndRun.ps1, winapp and analyzer assets are present. The imported copy specifies manual review and explicitly configured, pinned analyzers only.
3. Made MVVM/command conventions dependent on the selected toolkit and version; allowed correct ICommand patterns and collection replacement with proper notifications.
4. Kept x:Bind preference while allowing Binding where required; stable values may use OneTime binding.
5. Kept GPU scheduling within native context ownership instead of suggesting arbitrary UI worker dispatch for it.
6. Removed wildcard analyzer package installation from the reference.
7. Corrected the implication that all MSIX desktop apps are AppContainer-sandboxed, qualified the path-validation example, and removed unsupported Start/End alignment advice and the overly broad screen-reader visibility rule.

### ort-build

Added a project-scope preface: commands and upstream sibling references belong to a separate pinned ORT source checkout. Prefer prebuilt packages when sufficient. No build is allowed during M0. The rest of the upstream entrypoint is retained.

## NVIDIA and other CV material

NVIDIA's reviewed profiling entrypoint is explicitly for DeepStream SDK 9.0 on Ubuntu and derives throughput batch sizes from multi-stream microbenchmarks. That makes it unsuitable as an active skill here. Rejecting it is a fit decision, not a finding of malware. Its wider referenced resources were not audited because nothing was installed from it.

MMAction2, VideoMAE, TCN, CUDA and ORT documentation inform the custom skills. They are references rather than imported third-party Agent Skills. No claim is made that every NVIDIA/CV skill was exhaustively reviewed. Revisit a specific candidate only when it solves a demonstrated project need, with a fresh pinned review.
