# Tutorial contact review — Markov session 37B5B86D

**Result: two visible sword attacks, zero accepted point-contact / TTI targets.**
This is an AI visual review, not independent human ground truth. It covers
206 distinct source frames, including every successive frame in two short
candidate strike windows. It is not an exhaustive review of the recording.

Source: `MARKOV_SEKIRO_37B5B86D`, SHA-256
`17382ce410a120475b3f9d809f3e4fa7e9cc6978e4e628ab20da240c595a3dee`.
The original source and PTS index are in `data/sources/MARKOV_SEKIRO_37B5B86D/`.
The desktop exclusion at 0–260 seconds was respected. All frame numbers below
are zero-based absolute source frames; timestamps come from `source_pts.jsonl`.

| Candidate | Visible windup evidence | Detailed frame review | Contact decision |
| --- | --- | --- | --- |
| A, overhead sword attack | f24090, PTS 803000.000 ms: sword raised overhead toward Wolf | f24088–24111, PTS 802933.333–803700.000 ms | `OCCLUDED`; impact frame and TTI are null |
| B, overhead sword attack | f27895, PTS 929833.333 ms: sword and hands above the head, approaching crouched Wolf | f27892–27915, PTS 929733.333–930500.000 ms | `OCCLUDED`; impact frame and TTI are null |

In A, the sword remains overhead at f24095–24096; f24097 already shows the low
downstroke/followthrough pose. The attacker and grass cover the blade-to-Wolf
intersection. In B, f27896–27899 hold an overhead pose; f27900 already shows the
low pose with the bodies overlapping. Sparks and Wolf's subsequent reaction
do not reveal the physical contact point. No contact time was inferred from
HP, posture, the death overlay, or movement onset.

Both candidates provide visible attack-presence and windup evidence. Exact
windup, active, and recovery boundaries remain unresolved; the JSON preserves
pose anchors without presenting them as precise phase boundaries. Contact TTI
and safe-direction losses must remain masked for these candidates. The source
has 30 fps CFR timestamps, but adjacent images sometimes hold nearly identical
poses; unique timestamps do not prove 30 distinct motion observations per second.

The HUD displays **Leader Shigenori Yamauchi** at f24106 / 803533.333 ms and
f27914 / 930466.667 ms. The foreground attacker is an armoured swordsman with a
broad conical hat. Its identity was not verified from character appearance, so
this review adds no appearance-verified boss label. The HUD name is recorded
separately in the evidence JSON.

Other sampled portions show tutorial menus, unarmed traversal, roof movement,
and falls. In particular, the sampled sequences at 1015/1020/1025 seconds and
1075/1080/1095/1100/1105 seconds show roof/ledge movement and falling followed
by death. A death detector would produce false contact labels there. These
sparse observations are scene candidates, not continuous no-threat annotations.

The complete frame/PTS index, transforms, source grouping, review qualifications,
and contact-sheet hashes are in `markov_tutorial_contact_review.json`.
Local detailed sheets are:

- `data/reviews/markov_tutorial_contacts/strike_a_native_1.jpg` through `strike_a_native_3.jpg`
- `data/reviews/markov_tutorial_contacts/strike_b_native_1.jpg` through `strike_b_native_3.jpg`

Broader context is in `overview_1.jpg`–`overview_3.jpg`, `dense_1.jpg`–`dense_8.jpg`,
`encounter_a_1.jpg`–`encounter_a_5.jpg`, and `encounter_b_1.jpg`–`encounter_b_3.jpg`
in the same local directory. The sheets use source pixels with recorded
crop/resize and timestamp labels; no image enhancement or synthetic frames.

Source footage and contact sheets remain local research artifacts. The Markov
permission record permits the documented training use; it does not establish
rights to redistribute the footage. The textual review records local references;
the media remains excluded from Git.
