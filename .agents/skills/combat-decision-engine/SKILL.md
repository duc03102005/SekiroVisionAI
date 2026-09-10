---
name: combat-decision-engine
description: Design and audit SekiroVisionAI threat episodes, conservative dodge timing/direction, temporal hysteresis, duplicate suppression and fail-safe keyboard input decisions.
---

# Combat decision engine

Keep this engine separate from attack classification. A boss moving, a high attack score or a known animation ID is not enough to Dodge. During M0 only specify/review these rules. M9 evaluates shadow decisions; only M10 connects input after the earlier gates pass.

Read [decision-contract.md](references/decision-contract.md) for event identity, timing and concrete failure cases.

## Preconditions

Every prediction must carry source time, session/model version, Wolf and enemy track identity, attack phase/type, threat probability, confidence/quality, position/geometry uncertainty, motion coordinate frame, TTI interval and validity masks.

Reject or abstain on missing target/Wolf evidence, unsupported class, uncertain direction, poor track continuity, non-finite values, invalid probabilities, stale/nonmonotonic timestamps, model/provider faults, capture discontinuity or excessive TTI uncertainty. A null TTI is not zero. Check the configured foreground HWND/process before scheduling and again immediately before input.

Auto Dodge begins OFF and requires explicit arming in the application. Model changes and discontinuities invalidate pending actions and require re-established evidence. A missing/recovering model must never fall back to unconditionally pressing Shift.

## State machine

Use DISABLED, OBSERVING, CANDIDATE, ARMED, ACTED, REFRACTORY and FAULT with documented transitions. Only ARMED may schedule an action; only one in-flight action exists for a threat.

- Enter CANDIDATE when a plausible attack threatens Wolf. Require separate calibrated threat and confidence thresholds.
- Require sustained valid evidence for an elapsed-time dwell before ARMED. Valid future threats may accumulate dwell while their TTI is still too early for dispatch; the feasible action window gates dispatch, not the start of dwell. Use hysteresis: a higher entry threshold and lower exit threshold prevent flicker. Track continuity, source freshness and geometry remain mandatory while scores are hysteretic. Reset dwell after the configured maximum observation gap, while retaining any consumed strike token.
- Cancel ARMED on feint/recovery, loss of threat geometry, low exit scores, stale input, invalid timing or other precondition failure. Do not let smoothing manufacture confidence through missing frames.
- Recheck all preconditions at dispatch time. Atomically reserve the threat's action token before enqueueing; repeated inference callbacks cannot submit duplicates.
- Mark ACTED when an action is accepted for submission. In shadow replay, record a simulated acceptance and consume a separate shadow token at that same decision point; never describe it as an OS input submission. Submission failure cannot create an unbounded retry loop; invalidate/cancel and report the result. Preserve an audit trace of requested versus submitted input.
- REFRACTORY applies a global minimum action interval and retains the consumed threat token. Cooldown expiry alone never creates a new action for the same strike.
- A new threat requires evidence of a new strike/episode. Combo continuations can create separate strike IDs only with a demonstrated boundary and renewed threat geometry. Track loss/reacquisition must not silently reset the one-action rule.

## Timing and direction

Compute prediction age from aligned monotonic clocks. Convert source-referenced TTI to a remaining interval at decision time. Account for measured remaining scheduling/input/dodge-onset delay exactly once. Do not use UI refresh time as the reference or subtract already elapsed pipeline time twice.

Use a validated action window and TTI uncertainty to decide whether a dodge is still feasible. Abstain if the interval is too wide, overdue or unsupported; “imminent” is not permission to send a late useless dodge. Timing parameters describe measured system/dodge behavior and uncertainty, not a table of Animation ID → fixed delay.

Evaluate candidate directions using Wolf/enemy relative position, attack path, camera/lock-on transform, obstruction/arena-edge evidence and uncertainty. Document SCREEN, CAMERA, WORLD and W/A/S/D mappings. Opposite weapon motion does not automatically mean safe movement.

Treat the brief's slash/thrust/grab/projectile/AOE mappings as hypotheses to validate per geometry, not unconditional rules. A sweep or wide AOE may have no safe Dodge candidate. In that case abstain. Jump, Mikiri and Deflect remain out of the initial Auto Dodge scope.

## Input contract for M10

Support configured Dodge=Shift plus W/A/S/D chords with explicit scan-code/key mappings and measured press duration. Use the documented Windows input API; detect submission failure and integrity-level restrictions without inventing a bypass.

Maintain ownership of synthetic key-down events; release only owned synthetic keys with matching key-up events. Account for physical keys already held and conflicting player movement before acquiring a chord. An in-process cleanup handler cannot guarantee releases after process termination: define an independent bounded release/watchdog strategy and verify its failure modes before shipping. Do not claim crash safety from `finally`/RAII alone.

Stop, emergency hotkey, focus loss, capture/model fault and application shutdown cancel queued actions, release owned keys and clear/reinitialize inference state as appropriate. Emergency handling must not depend on a stalled UI dispatcher. Bound chord duration, cooldown, queue capacity and watchdog timeout. No periodic Shift spam, unlimited retries or press-and-forget actions.

Focus loss and emergency Stop latch DISABLED until a new explicit user arm command. Capture/model faults latch FAULT until recovery checks pass and the user re-arms. Rejecting a malformed, stale or out-of-order prediction alone does not prove a device fault; it resets candidate continuity as appropriate. Record all rejection reasons, with disable/fault/focus first, then invalid identity/time/evidence, consumed token/cooldown, score/dwell, and timing/direction. Reason ordering must not change action eligibility.

## Acceptance evidence

Evaluate full timestamped replay traces before live inputs. Measure false Dodge decisions per non-threat minute, false-decision proportion, missed-threat rate, timing error and duplicate-action count with explicit denominators. Require zero duplicate dispatch for one strike and zero dispatch after disable/focus-loss in deterministic trace checks. Test physical-key conflict and release/watchdog paths on Windows separately.

Choose numerical score/timing thresholds from held-out validation and measured input behavior; record them in configuration with provenance. The user's 0.97 example is not a validated threshold. Report unknown safe-direction coverage and unsupported attack types instead of claiming universal avoidance.

## Reference

- [Windows SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)

Before implementing, verify current API documentation, callback/threading behavior and foreground/integrity restrictions. Never bypass the milestone gate to test an action.
