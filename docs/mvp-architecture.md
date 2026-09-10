# Auto Dodge MVP architecture and provisional detector

Date: 2026-09-10. User direction: deliver a runnable vertical slice now. The [MVP scope](mvp-scope.md) supersedes all historical milestone gates; the eight reviewed skills remain intact.

## Working path and ownership

| Component | Work and ownership |
|---|---|
| Capture worker | WGC HWND → owned D3D11 texture → fullscreen-triangle grayscale resize → 256×144 RGBA staging texture. Map only after GPU completion; copy 36 KiB of gray pixels into a SmallFrame. The source frame lease remains retained until these GPU reads complete. |
| Latest-frame mailbox | At most one unprocessed SmallFrame. A newer sequence replaces pending work. A resize generation blocks old completions. |
| Vision worker | Compensated differences and sparse block flow, then causal temporal threat state. Never calls SendInput and never waits on the UI. |
| Input/hotkey worker | F8/F9/F10 registration, foreground identity and source-age checks, bounded one-request mailbox, one-episode reservation, global cooldown, key ownership and SendInput. Checks cancellation again before submission. |
| Watchdog process | Same EXE with an internal command-line mode. Shared ownership mask is published before key-down and cleared only after successful key-up submission. Parent exit or a 250 ms deadline triggers bounded release attempts. |
| UI / logger | UI snapshots and preview at 10 Hz; JSONL signal logs at about 5 Hz plus state/input events. Logging uses a bounded asynchronous queue. |

The intentional MVP tradeoff is a small GPU→CPU readback instead of waiting for CUDA/model integration. The staging image is 144 KiB per processed frame; the CPU gray image is 36 KiB. Full game textures are not CPU-mapped. When a CV sink is enabled, the existing GPU-stage timestamp includes the owned copy, resize and staging copy, not just the legacy M1 ownership copy.

## Detector semantics

The user draws a combat ROI while viewing the live grayscale capture, normally while lock-on keeps an enemy near a stable screen location. This is a manual ROI, not a trained enemy or weapon tracker. Motion centroid and local flow describe the ROI's moving pixels; they do not establish object identity.

Global camera translation is estimated by robust absolute differences over background samples outside the ROI, most HUD and Wolf's usual screen region. A +/-6 pixel search at 256×144 provides a coarse motion estimate. Large search-boundary movement, high background residual, a flash or a low-texture/black ROI suppresses arming. Sparse 3×3 patch samples on a grid search +/-3 pixels around the camera-compensated position. The score combines excess ROI difference, changed fraction, residual flow and rising motion energy. All features are causal and time-scaled. Frames closer than 7 ms are subsampled; gaps over 120 ms reset continuity.

Score and quality are **heuristic scalars**, not calibrated probabilities. There is no estimated TTI, enemy class, weapon pose, hitbox, attack type or validated safe direction. Camera rotation/zoom, foliage, effects and enemy locomotion can defeat translation compensation. Small attacks, slow windups, occlusion and combos without a quiet boundary can be missed. These are improvement targets, not blockers to connecting input.

## Threat and dispatch policy

Warm up 650 ms and observe a continuous quiet interval. A localized rising signal over entry/quality thresholds enters CANDIDATE. Sustain the lower hysteresis threshold for the configured dwell, emit THREAT_READY, and consume that motion episode's token. Continuing high scores cannot retrigger, even after cooldown. A continuous quiet interval plus cooldown permits a new episode. A camera cut or missing observation cannot retire a consumed token.

Before sending, InputService checks enabled state, the selected foreground HWND and process identity, source age <=120 ms, arm/config revision, key ownership, a consumed-episode guard and global cooldown. Shift/Ctrl/Alt/Win conflicts reject the action. A configured direction is added only if the user is not already pressing WASD. F10 uses the same input preconditions and shared global cooldown but is labeled as a manual test. Failure or partial SendInput submission disables input and releases owned keys; there is no automatic retry of the attack.

F9 and focus/capture invalidation cancel pending requests. Input threads poll at 5 ms while the UI and CV remain independent. Capture age over 150 ms disables input; dispatch uses the stricter 120 ms check. The 45 ms default key hold and 250 ms watchdog deadline are provisional operational settings, not measured game i-frame timing. Windows cannot target SendInput to an HWND atomically with a focus check, so a narrow OS focus race remains. Release is best effort under desktop/UIPI failures; logs distinguish requests, Windows submission counts and release attempts.

## Evidence and sources

Software tests exercise actual synthetic image sequences through CV→threat, idle/camera/black suppression, sustained-episode deduplication, quiet rearming, source FPS variation, stale/out-of-order rejection and dispatch cancellation. A Windows WARP test executes the real resize shader/staging path on known color quadrants and compares independent grayscale values. It is a functional test, not an RTX 3070 or Sekiro benchmark. The GUI smoke run creates controls, registers hotkeys and starts/stops the watchdog without sending keys. Live game accuracy and avoidance success remain unmeasured.

Microsoft references checked 2026-09-10: [SendInput return counts and UIPI](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput), [thread-owned RegisterHotKey messages](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey), [GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate), and [D3D11 Map and nonblocking flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map). Earlier WGC decisions and sources are in [ADR-002](adr/002-window-capture.md).

## Build

Use the existing Windows x64 preset, Visual Studio 2022/v143 and SDK 10.0.26100.0:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-release --parallel
ctest --preset windows-release
```

The runnable application is `out/build/windows-x64/Release/SekiroAutoDodge.exe`. No model download or external CV runtime is required. GitHub Actions packages the EXE, Vietnamese guide and build manifest. A source checkout also retains the earlier capture-only probe for diagnostics.
