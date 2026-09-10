# Runnable Auto Dodge MVP — current user direction

Effective 2026-09-10, this direction supersedes milestone gates in the original brief, historical milestone reports and Agent Skills. Preserve the skills and their provenance. Their technical advice remains useful; their old development-order restrictions do not apply.

Priority: runnable Windows application → capture Sekiro → actual Auto Dodge → reduce unwanted Dodge → improve learned models and UI later. Formal benchmarking, perfect datasets, a final AI model and all-boss coverage are not prerequisites.

The authorized first detector uses a user-selected combat ROI, small GPU-resized frames, background camera-motion compensation, frame differences, sparse block-matching flow and causal motion history. A heuristic score and temporal onset detector may schedule actual Shift or a configured W/A/S/D + Shift chord. Do not disguise this as a trained attack classifier, calibrated probability, predicted impact time or validated safe direction.

Keep Auto Dodge off at startup. Provide F8 to toggle while Sekiro is foreground, F9 for immediate disable/release, explicit target identity checks, stale-frame rejection, one action per detected episode, cooldown, hysteresis and bounded key hold. Log threat decisions, suppression reasons, input requests, actual SendInput return counts and releases separately. Capture or focus discontinuity disables input until explicitly re-armed.

Deliver code on a dedicated branch, a Windows executable/build, a short running guide, hotkeys and accessible logs. Improve accuracy and learned models after this end-to-end build exists. Software/CI results may be reported, but no milestone status may block the next implementation step.
