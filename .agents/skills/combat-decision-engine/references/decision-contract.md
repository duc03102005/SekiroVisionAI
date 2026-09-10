# Threat and dispatch contract

## Identity and temporal state

A threat key contains capture-session ID, stable enemy-track lineage, attack-episode ID and strike ID. Identity association survives short track loss when geometry/time still indicate the same strike. If identity is ambiguous, abstain rather than allocate a fresh action token.

Store first candidate source time, last valid source time, score hysteresis state, geometry validity, TTI interval, candidate direction, armed/consumed status and last dispatch result. Use elapsed milliseconds for dwell/cooldown, not a count of repeated duplicate frames. Reject out-of-order packets. Expire observations separately from action tokens; keep a token until its episode is conclusively retired.

Define maximum observation gap separately from maximum frame age. A gap resets dwell/history, not consumed tokens. Candidate dwell may start while TTI is valid but earlier than the dispatch window. In shadow evaluation, a would-dispatch decision commits a virtual token and cooldown; this models duplicate suppression without sending input. Supply every required prediction field or document explicit fixture defaults; missing fields in production remain invalid.

## Timing equations

For source time s, decision time d and source-relative TTI interval [a,b], the remaining contact interval is [a-(d-s), b-(d-s)] after clock alignment. A separately measured action-validity interval is defined relative to input submission and includes game/input processing and Wolf's evasive movement. Compare intervals in that same time basis. If dispatch occurs later, recompute at dispatch time.

Do not hide remaining latency in both the TTI correction and an empirical reaction offset. Report uncertainty from annotation, motion prediction, scheduling jitter and input consumption. The geometry model's predicted collision with current Wolf position is conditional on the stated motion assumption; player input can invalidate it before dispatch.

## Decision trace

Record session/event/strike IDs, source and decision times, age, phase/type, confidence and threat scores, TTI interval, thresholds/dwell policy version, geometry/direction validity, foreground/armed flags, state transition and reason code. Record NO_ACTION reasons as carefully as requested Dodge. Store no unrelated window contents or credentials.

Recommended reason codes: DISABLED, LOST_FOCUS, STALE_FRAME, OUT_OF_ORDER, INVALID_OUTPUT, LOW_CONFIDENCE, LOW_THREAT, INSUFFICIENT_DWELL, UNCERTAIN_IDENTITY, UNSAFE_DIRECTION, UNKNOWN_TTI, TTI_TOO_WIDE, TOO_LATE, ALREADY_ACTED, COOLDOWN, MODEL_FAULT and ACTION_SUBMITTED. These are categories, not a required enum implementation during M0.

## Behavioral review cases

- Repeated high-score callbacks for one strike, including after cooldown expiry.
- Confidence/threat flicker around entry and exit thresholds at different source FPS.
- A feint or canceled attack after ARMED but before dispatch.
- Identical image callbacks, out-of-order timestamps and old-session packets.
- Alt-tab or emergency Stop between scheduling and input submission.
- A new enemy/track ID that is actually the same partly occluded strike.
- A genuine new combo strike during or after global cooldown.
- A wide AOE/sweep with high confidence but no validated safe direction.
- Game/user already holding Shift/A/D; input submission partially failing.
- Worker/UI crash while a synthetic chord is held; independent release guarantee and its limits.

Run in shadow mode first. Software trace checks do not prove a successful in-game dodge or an OS-level key release after a crash.
