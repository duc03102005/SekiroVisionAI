# Local training sample recording

`SampleRecorder` is an opt-in Windows C++ component. It accepts only frames from
the selected Sekiro capture pipeline; it does not capture the desktop or inject
input. The app passes an actual `DODGE_SENT` event or a user review marker to
`mark(reason, episode)`. Recording starts disabled on every app launch.

## Output and timing

The output is a **JPEG frame bundle**, not an MP4 video. Windows Imaging Component
encodes color frames at JPEG quality 0.90 on a separate worker. The input is the
bounded color readback, up to 1280×720, without an additional recording resize.
The original capture dimensions are recorded separately. This is lossy imagery;
it is not the full GPU source texture or the 256×144 heuristic thumbnail.

The recorder samples at no more than approximately 15 frames per second and
retains about one second before a marker, then collects about one second after
it. Actual timestamps are retained; capture gaps are not filled with repeated or
fabricated frames. Sampling may be below 15 FPS when capture or the submitted
pipeline is slower. This frame rate supports sample review and baseline training;
it cannot provide finer temporal labels than the recorded evidence.

Each app session uses a new `session-<UTC date/time>-<PID>-<tick>` folder beneath
the directory supplied by the app. Each accepted marker produces a
`sample-000001` folder containing:

- `frame-000000.jpg`, etc.: zero-based sampled color frames.
- `sample.json`: source dimensions, build commit, capture generation, original
  frame sequence IDs, QPC source/readback times, marker time, episode ID and the
  available time before/after the marker.
- `frames.ffconcat`: playback/conversion timestamps derived from the actual
  gaps between consecutive source frames.

`sample.json` uses schema version 1 and format `sekiro-jpeg-frame-bundle`. Frame
`index` refers to the JPEG order, while `sequence` is the original capture frame
ID. QPC values are a monotonic clock within the Windows boot, not Unix timestamps.
The folder's UTC time identifies the recording session. The `marker.qpc_ms` value
is measured when `mark()` is called; the app's decision/input log provides the
associated model output and actual input result, correlated by episode and QPC.

`timing.pre_complete` and `post_complete` allow a sampling tolerance of 100 ms.
Always use `available_pre_ms`, `available_post_ms` and the per-frame timestamps
when computing labels or TTI. The `completion` field distinguishes a collected
window from recording disable, capture discontinuity, timeout, exit and budget
truncation. Missing future content is never padded. A sample marked immediately
after enabling recording can have little or no prehistory.

## Review labels are not ground truth

Every bundle starts with `annotation.status = "UNREVIEWED"`. Boss, phase, attack
class, impact, threat, TTI and dodge direction are null. A `FALSE_POSITIVE` or
`MISSED_ATTACK` marker is the user's review request, not an automatically trusted
negative or positive label. `DODGE_SENT` records that the input layer reported
submission; it does not establish that a Dodge succeeded or an impact existed.

Review the frames using the annotation workflow, set source/session provenance
and split assignments, and only then admit approved annotations to training.
No-hit/dodge footage with no observed impact must keep impact/TTI censored or
uncertain. All overlapping samples from one recording session belong to the same
train, validation or test group.

## Bounds and lifecycle

- Up to 17 frames of rolling history and 33 frames per sample.
- At most two outstanding samples, including collection, queueing and encoding.
  Further markers increment `dropped` and emit `SAMPLE_DROPPED`.
- Color buffers use shared immutable references; no frame image is copied or
  written to disk by `submit()`. At 1280×720, the conservative maximum raw buffer
  references total about 292 MiB, usually less because overlapping samples share
  frames. Encoding adds one BGR conversion and one bounded JPEG result at a time.
- A 2 GiB output budget per app session, checked before each file write. Encoding
  occurs in memory before budget admission. Metadata has a separate 64 KiB
  reserved allowance within the same budget. Reaching the budget disables
  recording until the next app session; it does not delete older recordings.
- Existing session folders are preserved. **The budget is per session, not a
  global retention limit**: review/delete old sessions when no longer needed.
- Stop/resize/focus loss can call `discontinuity()`. Missing postframes time out
  after the requested window plus 250 ms. Disable and orderly app exit flush the
  already collected bounded samples as truncated; they do not wait for future
  game frames. Exit waits for at most the two admitted encoding jobs. A forced
  process termination can leave an incomplete folder. A final `sample.json` is
  published only after all admitted images and the concat file are closed;
  importers must reject folders without that final manifest.

The event callback emits `RECORDING_ENABLED`, `RECORDING_DISABLED`,
`SAMPLE_MARKED`, `SAMPLE_SAVED`, `SAMPLE_DROPPED` or `RECORDING_FAULT`. Status
reports enabled state, outstanding samples, saved/dropped counts, bytes written
and the most recent reason. Disk failures do not block capture or trigger input.

## Optional MP4 conversion

FFmpeg is an offline conversion dependency, not part of the native app. From a
sample folder, with FFmpeg installed, run:

```powershell
ffmpeg -f concat -safe 0 -i frames.ffconcat -fps_mode vfr -c:v libx264 -pix_fmt yuv420p review.mp4
```

The concat file uses only generated relative image names and sets a 1 ms input
time base. It does not repeat the last frame to manufacture posthistory. Video
containers can round timestamps; **`sample.json` is the timing authority for
annotation and training**. Keep it alongside converted video. Do not use
`-framerate 15` on the JPEG pattern to infer impact times, because real sample
intervals can vary. This command is for review; conversion availability depends
on the codecs included in the user's FFmpeg build.

## Validation

`sample_recorder_self_test()` performs a native Windows WIC JPEG encode/decode
and checks color channels, dimensions, the two-marker queue bound, default-off
behavior and truncated/unreviewed JSON output. It uses generated test pixels,
does not capture Sekiro and never sends keys. Its output is not gameplay data.
Run it in a temporary directory; CI may delete that directory after the check.

Technical references: [Microsoft WIC encoding overview](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-creating-encoder),
[pixel format negotiation](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-setpixelformat),
[COM memory stream ownership](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-createstreamonhglobal),
and [FFmpeg concat demuxer](https://ffmpeg.org/ffmpeg-formats.html#concat-1).
The FFmpeg concat/timestamp behavior was also checked against Context7's official
FFmpeg documentation index on 2026-09-10.
