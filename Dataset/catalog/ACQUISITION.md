# Reacquiring the real gameplay sources

`acquired_sources.json` locks real media identities, publisher/distributor provenance, permissions, actual decoded PTS metadata and known training exclusions. Source videos stay outside Git. The catalog is acquisition evidence, not an attack label set or an all-boss evaluation result.

The development/CI machine needs Python and FFmpeg; the Windows application does not. Reacquire one approved source with:

```bash
python -m DatasetTools.downloader.acquire_catalog --source-id LIVE_YT_GAMING_SEKIRO_005 --root data
```

Repeat `--source-id` to select several sources. Omitting the filter selects the entire acquired catalog, including the larger Markov recordings. The per-source limit defaults to 1 GiB. Use a separate data root to verify a clean re-download. Existing sources are reused only after their actual file SHA256 matches the locked source.

The six LIVE-YT-Gaming IDs end in `005`, `016`, `019`, `023`, `026`, and `029`. They are distributed in the Q-Bench-Video authors' publicly accessible archive at a pinned Hub commit. The importer requests the exact unencrypted ZIP entry, verifies the local member name, compression type, size and CRC32, then verifies the extracted file SHA256 before standard ingestion. It never extracts a platform watch page. The original LIVE-YT-Gaming permission notice accompanies each source. The full archive is not claimed to have been downloaded or hashed.

The Markov IDs are `MARKOV_SEKIRO_A7ECDCA8` (Gyoubu and Isshin combat, with menus) and `MARKOV_SEKIRO_37B5B86D` (opening/tutorial/traversal). Their complete MP4 bytes are verified against the publisher's pinned LFS SHA256. The publisher directly invites training on its linked gaming data; this contextual invitation is documented in `permissions/markov-training-use.json`. It is not a named open license or an original-media redistribution grant. Do not publish these recordings or their interaction logs.

Each successful import produces the original source, its source-frame/PTS map, permission evidence, `catalog-provenance.json` and `training-exclusions.json`. The latter contains coarse gameplay selection and exclusion intervals, not attack/contact/dodge annotations. The Markov publisher's pure-gameplay description is imperfect: visual review found incidental non-game overlays. Exclude those intervals and use reviewed gameplay regions for mining/training. Do not infer visible contact or TTI from an input event, health bar change, boss title or successful avoidance.

All six LIVE-YT clips stay in one conservative group because original player/session identities are unknown. The two Markov sessions have distinct session IDs but share an unknown-player duplicate group; their clips must not cross training/validation/test boundaries. A different filename or a compilation excerpt is not evidence of an independent player. Report missing split/boss coverage explicitly.

Validation performed during acquisition: four local ZIP/URL corruption and identity tests; a fresh public HF re-download of `LIVE_YT_GAMING_SEKIRO_023` through this exact utility, with SHA256 and 245 decoded PTS-indexed frames verified. No synthetic video was used as gameplay evidence.
