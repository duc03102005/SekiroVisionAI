# Source splits

The empty lists in this directory are an honest initial state: catalog URLs are
not acquired, reviewed training data. Do not put reference-only URLs into these
files or claim that an empty test measures generalization.

Generate a new immutable split version after permitted media has been imported:

```powershell
python -m DatasetTools.validation.splits --sources data/source_manifest.jsonl --output data/splits/v001 --held-out-boss "Guardian Ape"
```

The command groups source, creator/player, recording session, duplicate group,
content hash and canonical source URL before assignment. A source containing a
held-out boss is kept entirely in test, together with any linked creator/session
sources. `heldout_boss_test_sources.txt` and `unseen_source_test_sources.txt` are
disjoint subsets of `test_sources.txt`. Thresholds belong to validation only.

Version actual source-ID lists, annotations and manifests in Git after removing
machine-local paths; keep original media, encoded clips and weights out of Git.
