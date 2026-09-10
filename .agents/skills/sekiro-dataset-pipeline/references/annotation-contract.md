# Annotation contract v1

The [example](annotation.example.json) is synthetic documentation, not a labeled recording. The field definitions and semantic invariants below define the proposed format. Machine-readable schema and ingestion validation will be implemented with the M3 dataset pipeline.

## Classes

| Dimension | Values |
|---|---|
| movement | IDLE, WALK, RUN, TURN, FEINT, UNKNOWN_MOVEMENT |
| phase | ATTACK_WINDUP, ACTIVE_ATTACK, RECOVERY, COMBO_CONTINUATION, UNKNOWN_PHASE |
| attack_type | HORIZONTAL_SLASH, VERTICAL_SLASH, DIAGONAL_SLASH, THRUST, SWEEP, GRAB, JUMP_ATTACK, PROJECTILE, AOE, UNKNOWN_ATTACK |

Labels may change within one event. The example's `phase` and `movement` refer to its `anchor_frame`; boundaries describe the event interval. A subsequent annotation format can add interval tracks without overloading these scalar fields.

## Frame and evidence semantics

All frame indices are zero-based, absolute within the normalized source timeline, with half-open clip interval `[clip_start, clip_end)`. `fps_num/fps_den` is rational. Frame-derived milliseconds are `frame * 1000 * fps_den / fps_num`; preserve actual original PTS separately and do not equate nominal FPS with VFR timestamps.

`windup_start`, `active_start`, `impact_frame`, `recovery_start` and `dodge_start` use that same frame space. Unknown/out-of-clip events are null, not 0. Non-null phase boundaries follow windup ≤ active ≤ recovery and lie within the clip. Contact may occur only within the annotated active interval. `anchor_frame` lies within the clip. These cross-field comparisons require semantic validation; JSON Schema alone does not enforce them.

`impact_evidence` values:

- `OBSERVED_CONTACT`: visibly verified contact; `impact_frame` required.
- `ESTIMATED_COUNTERFACTUAL`: reviewed interval for projected contact under a recorded motion assumption; point impact is null and `estimated_contact_interval` is required.
- `NO_CONTACT`: an observed miss/avoidance; no point impact.
- `OCCLUDED`: contact cannot be resolved visually; no point impact.
- `OUT_OF_CLIP`: event is censored by clip boundary; no point impact.

`dodge_direction` is in the stated screen/camera coordinate frame and means observed movement, not a recommended key. `dodge_timing_evidence` is INPUT_LOG, VISUAL_ONSET or NOT_OBSERVED. An impact is not inferred merely because Dodge succeeded. `would_hit_without_dodge` is TRUE, FALSE or UNKNOWN; only explicit counterfactual review can move it out of UNKNOWN.

`confidence` is annotation quality assigned by the reviewer, not the model's calibrated prediction. Keep free-text notes non-executable. Record the source/group and annotation revision as stable IDs. Training masks must separate contact observation from estimated/censored targets.
