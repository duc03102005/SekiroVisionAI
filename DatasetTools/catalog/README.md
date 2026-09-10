# Update dataset coverage

From the repository root, after importing and reviewing clips:

```console
python -m DatasetTools.catalog.update_coverage
```

Defaults: `Dataset/catalog/video_sources.csv`, `data/source_manifest.jsonl`,
`data/clips.jsonl`, `data/annotations.jsonl`, and
`Dataset/catalog/boss_taxonomy.json`. The command rewrites
`Dataset/boss_coverage.csv` atomically. Each input/output has an explicit CLI
override. Missing manifests produce zero counts and are listed in the JSON
summary. Existing boss/phase rows and all taxonomy targets are preserved;
new reviewed boss phases receive additional rows. Unknown phases remain
`UNREVIEWED` and are not an all-phase total.

Only nonsynthetic `IMPORTED` sources with a recorded real usage basis/evidence,
SHA-256 identity and a local nonempty media file can contribute. Their clips
must exist locally and identify the same source hash. Only the latest annotation
revision can contribute, and it must be `reviewed` under the v2 annotation
contract. A later rejection removes earlier reviewed coverage. The report uses
manifest hashes as identities; it does not rehash/decode large videos on each
inventory update. Run media validation separately if files may have changed.

Numeric definitions, for each boss and boss phase:

- `number_of_sources`: distinct acquired recordings represented by reviewed
  clips. Source SHA, declared duplicate groups and original URLs collapse
  duplicate uploads transitively. Unreviewed imports are reported only in the
  command summary.
- `total_minutes`: union of the original PTS intervals of reviewed **clip
  contexts**, grouped by recording. Overlapping clips are not added twice.
  This is not a count of fully labeled frames; an event can leave context
  unlabeled. Separate phase rows can share a clip crossing a phase boundary,
  so do not sum phases into a global duration without another interval union.
- `attack_clips`: unique original clip extents containing a reviewed attack
  event, including attacks with unknown or false danger labels.
- `positive_attack_clips`: the subset with explicit reviewed
  `threat_label: true`. An attack is not automatically dangerous.
- `negative_clips`: reviewed `ENTIRE_CLIP_NON_THREAT` clips with a negative
  reason and `threat_label: false`. An avoided attack is not automatically a
  whole-clip negative. Conflicting whole-clip negative and attack reviews fail
  before replacing the report.
- `different_players`: known imported `player_id`, otherwise creator,
  case-insensitively deduplicated. This reports recorded identities, not proof
  that separate channel names are separate people.
- `different_camera_conditions`: distinct optional `camera_conditions` tags
  explicitly present in reviewed annotations. The field may be a string or
  list of strings. Unknown tags are excluded. Current annotation UI does not
  populate these tags; the count remains zero until explicitly reviewed.

Clip extents repeated across duplicate sources count once. Multiple strike
annotations in one clip do not manufacture extra clips. Candidate catalog URLs
contribute only a reference count in notes.

The operational inventory score, from 0 to 100, is:

```text
20 * (min(sources / 3, 1)
    + min(positive_attack_clips / 50, 1)
    + min(negative_clips / 50, 1)
    + min(different_players / 3, 1)
    + min(different_camera_conditions / 3, 1))
```

`EMPTY` means no reviewed real clips. Other rows are `POOR` when either
positive threats or whole-clip negatives are absent, or the score is below 40.
With both types present, scores 40–64.99 are `USABLE`, 65–84.99 `GOOD`, and
85–100 `STRONG`. These are collection labels and adjustable inventory targets,
**not measured accuracy, generalization, model acceptance, or development
gates**. A small real dataset can begin training while this score is low.

Run the bounded provenance/count checks:

```console
python -m unittest DatasetTools.catalog.test_update_coverage
```
