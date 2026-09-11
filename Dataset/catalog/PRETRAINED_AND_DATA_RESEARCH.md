# Released models and additional gameplay — checked 10–11 September 2026

The bounded primary-source search found **no immediately reusable Sekiro temporal
attack/TTI model with documented weights permission and verified broad boss
coverage**. This is a search result, not a claim that no such model exists.
No additional gameplay files, full model weights, or contact labels were added.
The eight previously acquired source videos remain the actual dataset.

| Source | Concrete material checked | Practical limit |
| --- | --- | --- |
| [NVIDIA NitroGen](https://huggingface.co/nvidia/NitroGen) | Official `ng.pt`, 1,974,723,762 bytes; ordinary public HTTP range access verified | General gamepad policy, last-frame input, no attack/TTI head; NVIDIA non-commercial research terms |
| [XuLvXiu Sekiro classifier](https://github.com/XuLvXiu/sekiro_classifier_ai) | `model.resnet.v1`, 44,794,123 bytes, listed by GitHub; action labels and collection code | Genichiro phase-one experiment; no declared repository/weight license located |
| [XuLvXiu Sekiro DQN](https://github.com/XuLvXiu/sekiro_dqn_ai) | `model.resnet.v2`, 44,796,171 bytes, and an LFS checkpoint pointer | Author reports Genichiro-specific states and poor generalization; no declared license located |
| [RongKaiWeskerMA Sekiro](https://github.com/RongKaiWeskerMA/sekiro_play) | MIT training code, 30 sessions of action CSV/metadata | Checked tree has no gameplay frame sequences; checkpoint directory contains only a placeholder |
| [Analoganddigital DQN](https://github.com/analoganddigital/DQN_play_sekiro) | MIT training/input/capture code | Checked repository root has no released checkpoint or recorded gameplay dataset |
| [RAFT](https://github.com/princeton-vl/RAFT) | Author-published pretrained flow models linked by a download script; repository BSD-3-Clause | General optical flow, no Sekiro contact/attack supervision; archive-specific notices and native export not validated |
| [Full LIVE-YT-Gaming](https://live.ece.utexas.edu/research/LIVE-YT-Gaming/index.html) | Original 600-video database with explicit reuse grant | Publisher asks for a form submission to email the full download link; no expanded direct Sekiro index verified |
| [GameScope](https://rajeshsureddi.github.io/GameScope/) | Public gallery with 22 directly hosted video samples | None of those sample rows names Sekiro; full-set access is a request form |
| [VideoGameBunny](https://huggingface.co/datasets/VideoGameBunny/Dataset) | Public screenshot/instruction dataset and released VLM research | Static image tasks, no contact timeline; Sekiro coverage and original-creator permission chain not verified |

NitroGen is the strongest concrete general gaming-model lead. Its official
checkpoint is pinned at revision `584c8dded734d032f07a4bcc0ccb330e703298c4`.
The publisher's SHA-256 is
`a266f5fb9c7dbdcdf97216558d2d82075a9a994b824cda69afa9fd3280260a81`.
The access probe returned `206 bytes 0-31/1974723762`; only 32 bytes were read,
so the complete checkpoint hash was **not** recomputed. The supplied weight
[license](https://huggingface.co/nvidia/NitroGen/blob/584c8dded734d032f07a4bcc0ccb330e703298c4/LICENSE)
restricts use to non-commercial research and preserves that restriction for
derivatives. Its actual licence file was retrieved and hashed.

The model has approximately 493 million parameters and predicts gamepad action
chunks from a 256×256 image. The authors explicitly state that the current
model sees only the latest frame and cannot be assumed to complete games or
handle completely unseen games. No official ONNX artifact appeared in the
checked model file list. Converting its action outputs into a temporal enemy
threat/TTI detector would be a separate research task; no Sekiro boss coverage
or success rate was established here.
[Author limitations](https://github.com/MineDojo/NitroGen#nitrogen)

The [NitroGen dataset card](https://huggingface.co/datasets/nvidia/NitroGen)
explicitly releases **gamepad action annotations only**, with synthetic labels
and CC BY-NC 4.0 terms. It does not distribute the source videos. These player
action labels also do not establish when an enemy weapon touches Wolf, or
whether a recorded dodge direction is safe. It therefore does not fill the
missing contact supervision or provide another acquired Sekiro video corpus.

The [GameScope paper](https://arxiv.org/html/2605.01272v1) describes 424 original
clips across 74 games, expanded through encoding to 4,048 quality-assessment
samples. It reports Creative Commons UGC sources and direct PS5 captures.
Those counts must not be read as 4,048 independent fights. The public
[gallery manifest](https://github.com/rajeshsureddi/GameScope/blob/febd55193c1af612a3caa321281128402d301abe/data.json)
has 22 sample rows and no Sekiro row; Sekiro's presence in the complete dataset
remains unverified. The site specifies academic research use for media. Its
repository software license was not treated as a blanket media license.

No access forms were submitted, no creators were messaged, and no platform
download controls or encrypted/gated OpenSIMA material were bypassed. A public
repository or a paper's broad performance claim was not treated as weights
permission or evidence of all-boss gameplay performance.

Machine-readable availability, licence qualifications, source URLs, pinned
identifiers, and decisions are in
`pretrained_and_data_research_2026-09-10.json`. Retrieved licence/cards and the
NitroGen access probe are local research evidence under
`data/research/pretrained_sources_2026-09-10/`. No runtime or training changes
were made for this investigation.
