# Independent shadow-decision exercise

Date: 2026-09-10. Input: [decision-trace.json](decision-trace.json). Method: a fresh agent read only the revised combat skill/reference and the fixture, applied its rules, and returned the following outcomes. No program or game input was executed.

| Row | Shadow decision | Reason |
|---|---|---|
| 0 | NO_ACTION | Disabled |
| 1 | NO_ACTION | Dwell 0/40 ms; remaining TTI [175,195] ms is early |
| 2 | NO_ACTION | Dwell 20/40 ms; [130,150] exceeds the validated window |
| 3 | WOULD_DISPATCH | Dwell 45 ms; [95,115] fits [70,140]; consume virtual strike A token |
| 4 | NO_ACTION | Already acted; cooldown not elapsed; [60,80] also too late |
| 5 | NO_ACTION | Already acted despite cooldown expiry; long gap resets dwell, not token |
| 6 | NO_ACTION | New strike B with dwell 0; timing still early |
| 7 | NO_ACTION | Lost focus; latch disabled and cancel pending action |
| 8 | NO_ACTION | Explicit re-arm, but source age 100 ms exceeds 50 ms |
| 9 | NO_ACTION | No validated safe direction |
| 10 | NO_ACTION | Unknown/null TTI |
| 11 | NO_ACTION | Stale, out-of-order, and overdue |

One virtual acceptance; zero duplicate virtual acceptances and zero virtual acceptances after focus loss. These counts refer only to this documented exercise. They are not real engine performance or game-safety metrics.

The first pass used incomplete defaults and revealed ambiguity. The revision explicitly distinguishes early candidate dwell from the dispatch window, defines virtual acceptance/token consumption, preserves consumed tokens over long observation gaps, and requires a user re-arm after focus loss. A separate fresh pass produced the table above without an expected-answer key.

Timestamp watermark handling after a rejected packet remains an implementation choice; row 11 is stale regardless. No outcome-changing ambiguity remained for this fixture. Other scenarios in the decision contract still need engine-level replay tests and Windows execution at the appropriate milestone.
