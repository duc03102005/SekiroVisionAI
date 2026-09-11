# Source and boss holdouts

`Training.datasets.splits` unions source, player, session and duplicate IDs
transitively. A complete connected group stays in one split. With fewer than
three independent groups the validation/test files remain empty; this reports
missing evidence instead of manufacturing a frame split.

Pass `--holdout-boss "Lady Butterfly"` (repeatable) before training. If that boss
shares a source/player/session group with another boss, the entire group moves
to `heldout_boss_sources.txt`. The remaining groups form train/val/test. This can
be conservative for one creator with many uploads, intentionally exposing the
actual source diversity rather than overstating generalization.

The trainer fits temperature and thresholds on validation only, then evaluates
test and held-out-boss sets once with frozen settings. Reports retain per-boss
and per-class denominators, missing/censored TTI counts, and unavailable live
metrics. `test` is unseen source/player under recorded identities;
`heldout_boss` is unseen boss and source group. Unknown or incorrectly recorded
creator/session/duplicate IDs still require provenance review.
