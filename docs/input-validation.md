# Native input validation

The production `InputService` submits physical scan codes through Windows `SendInput` and owns only the synthetic keys it acquires. A request can select Shift alone, A, D, S or W plus Shift. A player already holding W/A/S/D keeps that direction; the service adds only Shift. Existing Shift/Ctrl/Alt/Win input suppresses a new Dodge.

## Arming and cancellation

Auto Dodge starts OFF. F8 toggles it in the selected, verified Sekiro window. F9 latches OFF and releases owned keys. The input thread also polls F9 independently because an unmodified registered hotkey can be hidden by the Shift that the service itself is holding. Emergency handling does not depend on the UI dispatcher.

The application's Auto Dodge button calls `InputService::toggle()`. If the detector, selected process identity, capture and watchdog are available while the application has foreground, the explicit command enters `arm_pending`. It has a 15-second limit. Only the selected game becoming foreground with a valid source timestamp less than 120 ms old activates it. No window focus is forced. A second toggle, F9, Stop, model failure, capture discontinuity or configuration change cancels the pending arm. Once ON, loss of focus latches OFF; simply returning to the game does not re-enable it.

Capture source times remain the original monotonic capture timestamps. Invalid, non-finite or future times cannot arm the service. Dispatch checks both the latest capture timestamp and the request timestamp. A stale request cannot become fresh because a new preview or frame arrives.

Cancellation and the final submission share one mutex. Thus a cancellation that acquires the dispatch lock first invalidates the queued command before `SendInput`. The final foreground and deadline checks have no logging or callback between them and submission. Windows does not provide an atomic transaction combining foreground ownership with `SendInput`, so the process cannot promise that Windows will never switch focus in the remaining OS scheduling interval.

The one-slot queue, arm revision, consumed episode, global cooldown and source-referenced deadline remain enforced at dispatch. Invalid direction values, malformed/non-finite TTI windows and unsupported readiness are rejected. Partial insertion latches OFF and releases the accepted prefix of the chord. A zero-insertion failure acquires no new keys. Retries are bounded.

## Independent release

Normal holds are configurable from 30 to 90 ms. Ownership and a 250 ms deadline are published to a separate same-executable watchdog before key-down. The watchdog releases if the input thread stalls, the deadline expires, shutdown is requested or the parent process exits. A watchdog trip latches a fault; a fresh frame cannot silently re-arm it.

The parent has at most three release attempts and the independent child at most six. The application reports release failure. This is a bounded recovery strategy, not an absolute guarantee against Windows rejecting input or the OS itself failing. It does not bypass UIPI or an integrity-level restriction.

## Windows integration test

`Tests/InputServiceTests.cpp` compiles the **same** production `InputEngine/src/InputService.cpp`. `SVAI_INPUT_TESTING` adds constructor hooks only to this test target; these hooks are absent from the application. They restrict target identity to the benign receiver child launched by the test and allow two explicit fault injections. The receiver records the tagged keyboard messages it actually receives through Windows. The real `SendInput` backend and independent watchdog process execute in the tests.

The controlled fresh-frame feeder stands in for capture **only for this isolated input test**. It does not test WGC, model accuracy, Sekiro accepting input or gameplay dodge timing. The test never targets `sekiro.exe` or an unrelated window.

Assertions cover:

- Explicit UI pending arm, foreground activation and missing-detector rejection.
- Actual registered F8 ON/OFF and F9 release during an owned Shift chord.
- Actual A+Shift key-down/key-up, measured receiver hold duration and per-request direction override.
- Global cooldown, duplicate episode suppression and stale/NaN/future requests.
- Invalid TTI, invalid direction and an invalid capture clock that stays OFF after freshness recovers.
- A queued command cancelled before dispatch by disable, focus loss or model failure.
- Existing physical modifier conflict and preservation of player-owned W movement.
- Actual one-key insertion followed by an injected partial-return result; only the accepted key is released and no complete Dodge is counted.
- Independent watchdog release while the input submission thread is deliberately stalled.
- Independent watchdog release after a separate input-parent process terminates immediately after a real accepted key-down. Its C++ destructors do not run.

A successful invocation prints individual `INPUT_TEST_OK` lines and `INPUT_TEST_RESULT verified actual Windows SendInput receiver delivery and release`. If Windows provides no interactive input desktop, denies the initial controlled foreground operation or has conflicting required hotkeys, the executable prints `INPUT_TEST_UNAVAILABLE` and exits 77. Configure CTest with `SKIP_RETURN_CODE 77`, `TIMEOUT 45` and `RUN_SERIAL TRUE`. A skipped test is **not** evidence that Windows input was verified. A failure after input testing begins exits 1.

No Windows runtime is available in the Linux development container; Windows CI results, including any skips, must be inspected before recording actual execution evidence. No live-game success rate is inferred from this test.

## API references checked 2026-09-10

- [Microsoft SendInput documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput): event counts, serial insertion, existing keyboard state and UIPI restrictions.
- [Microsoft KEYBDINPUT documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-keybdinput): scan-code events, key-up and extra-information tagging.
- [Microsoft SetForegroundWindow documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow): Windows foreground restrictions; the application does not force focus.
- Context7 `/websites/learn_microsoft_en-us_windows_win32_api` was used to cross-check scan-code and keyboard-state semantics.
