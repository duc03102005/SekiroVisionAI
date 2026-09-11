# Automatic target tracking contract

`VisionEngine/include/SekiroVisionAI/TargetTracker.h` provides one portable,
causal tracker for the native application and ReplayHarness. `process(frame,
detections)` produces image-space actor observations and a dynamic `CombatRoi`;
`reset()` clears observation history without recycling track IDs. No manual ROI
is needed to run the provisional visual-region path.

## Semantic detector input

`TargetDetection` identifies a `Wolf` or `Enemy` role, a normalized full-image box,
confidence, detector track ID if available, and the **original** generation,
sequence and source timestamp. `semantic_supported` must be explicitly true
only when a trained detector supports that role. Generic `person` detections,
motion blobs and untrained model outputs are not Wolf or enemy annotations.

The native `TargetDetection` runtime now supplies model observations, and
`Training/targets` trains/export candidates from acquired gameplay with explicit
AI-reviewed role boxes. The compact candidate does not generalize to the first
Gyoubu source check; no current evidence demonstrates all-boss recognition.
Callers must not manufacture semantic detections from the fallback ROI. See
`Training/targets/README.md` for actual training, native parity and failure
evidence. The tracker itself does not embed model weights.

Only observations from the exact current generation and sequence are accepted;
their timestamp must agree within 0.5 ms. A sparse or asynchronous detector must
use its original source frame and explicit tracking before returning current
observations. Assigning the current timestamp to an old box is invalid.

The tracker selects a continuous target, confirms repeated observations for an
elapsed-time dwell, and preserves the association through short observation
loss. A different detector ID cannot replace a held target immediately. Similar
competing detections produce `TARGET_AMBIGUOUS`. Unconfirmed tracks need a new
uninterrupted confirmation interval after missing evidence.

`identity_certain` requires confirmed, non-conflicting **current-frame** Wolf and
enemy semantic observations. Held boxes remain available for display with their
true age but are invalid for action. Actor-pair `track_lineage` changes when
either Wolf or enemy identity changes. The temporal model must restart its crop
history on a lineage change; the threat engine must retain consumed action tokens
according to its own discontinuity and episode policy.

## Provisional automatic ROI

Without a semantic detector, `allow_provisional` enables an explicitly labeled
visual-region tracker:

1. Estimate bounded camera translation from background grayscale samples while
   excluding tracked regions, most HUD and a documented lower-center screen
   prior. This prior is not Wolf detection.
2. Subtract camera motion and global brightness change, then collect localized
   residual-motion components. Reject widespread residual motion, camera jumps,
   flashes, low texture and ambiguous candidates.
3. Require temporal confirmation before making a region valid. Track the region
   with a bounded image patch search and new motion observations; preserve crop
   extent when only one edge or limb moves.
4. Expand the region for context and follow it automatically. With real semantic
   actor detections, the crop contains both actors, subject to the current
   preprocessing bounds of x=0.03–0.97 and y=0.03–0.92.

The fallback uses the small grayscale image only for inexpensive tracking. The
temporal model continues to crop color AI frames; the tracker does not substitute
the debug thumbnail for model input. Its stored history copies only grayscale,
timestamps and identity fields, without retaining a capture color/GPU lease.

For a provisional region, `valid` means a visual region is currently tracked;
`enemy.semantic_confirmed`, `wolf.valid` and `identity_certain` remain false.
The reason is `PROVISIONAL_VISUAL_REGION_NO_SEMANTIC_IDENTITY`. This signal is
usable for explicit heuristic/debug operation and clip mining, but cannot prove
that the moving region is a boss instead of Wolf, particles, grass or an occluder.
Rotation/parallax beyond the translation approximation can force abstention.

## Geometry and direction

Boxes use normalized screen coordinates, +x right and +y down. `AttackPath` and
`EscapeEvidence` are separate, initially invalid contracts. Target displacement
is never copied into an attack/weapon trajectory, and free image margin does not
establish a traversable world-space escape. Direction choice must preserve this
distinction and identify any provisional lateral preference as a heuristic.

## Verification

`Tests/TargetTrackerTests.cpp` runs native deterministic tests for automatic
region acquisition/following, camera-only rejection, black/cut invalidation,
semantic confirmation, stale/out-of-order rejection, target replacement, Wolf
identity changes, ambiguity, unsupported labels, true held-observation age,
capture generation resets, and capture color ownership release. The images and
detections are synthetic software fixtures; these tests do not measure semantic
accuracy on Sekiro gameplay.
