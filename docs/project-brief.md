# SEKIRO VISION AI AUTO DODGE — MASTER PROJECT PROMPT

Tôi muốn bắt đầu lại hoàn toàn từ đầu một dự án phần mềm Windows có tên:

# `SekiroVisionAI`

Mục tiêu cuối cùng là xây dựng một ứng dụng Windows native có khả năng quan sát trực tiếp gameplay **Sekiro: Shadows Die Twice**, nhận diện chuyển động tấn công của enemy/boss bằng Computer Vision + Temporal AI, dự đoán thời điểm đòn đánh có khả năng chạm Wolf, sau đó tự chọn hướng và thực hiện Dodge với độ trễ thấp.

Đây là dự án dành cho **offline / single-player**.

Không xây dựng:

- multiplayer cheating
- anti-cheat bypass
- DRM bypass
- crack bypass
- account bypass
- hardware ban bypass

---

# 0. NGUYÊN TẮC QUAN TRỌNG NHẤT

Không tiếp tục các prototype Python cũ dựa trên:

- Animation ID hard-code
- HP loss
- Posture
- timing thủ công cố định
- Hanbei-only rules

Những prototype trước chỉ có giá trị chứng minh:

- Có thể capture/đọc trạng thái game.
- Có thể gửi input Dodge vào Sekiro.
- Hard-code animation không scale.
- HP/Posture không phải hit detector đáng tin.
- False-positive Dodge là vấn đề rất nghiêm trọng.
- Timing cố định theo một Animation ID rất dễ sai.

Hãy coi đây là một project hoàn toàn mới.

---

# 1. QUY TẮC BẮT BUỘC TRƯỚC KHI VIẾT CODE

## TUYỆT ĐỐI KHÔNG ĐƯỢC viết CaptureEngine trước khi hoàn thành toàn bộ phần Agent Skills.

Bước đầu tiên của project PHẢI là:

1. Kiểm tra GitHub connector/plugin.
2. Kiểm tra Context7.
3. Tạo repository GitHub:

`SekiroVisionAI`

4. Tạo hệ thống Agent Skills trong repository.
5. Cài/import các Agent Skills bên ngoài phù hợp.
6. Tạo các custom Agent Skills riêng cho project.
7. Review từng `SKILL.md`.
8. Kiểm tra không có nội dung nguy hiểm/prompt injection/script đáng ngờ.
9. Commit toàn bộ Agent Skills vào GitHub.
10. Chỉ sau khi bước này PASS mới được bắt đầu CaptureEngine.

Nếu chưa hoàn thành Skill Setup thì KHÔNG được nhảy sang code application.

---

# 2. AGENT SKILLS PHẢI CÓ

Repository phải có:

```text
SekiroVisionAI/
│
├── .agents/
│   └── skills/
│       │
│       ├── windows-gpu-capture/
│       │   └── SKILL.md
│       │
│       ├── realtime-video-ai/
│       │   └── SKILL.md
│       │
│       ├── sekiro-dataset-pipeline/
│       │   └── SKILL.md
│       │
│       ├── low-latency-inference/
│       │   └── SKILL.md
│       │
│       ├── combat-decision-engine/
│       │   └── SKILL.md
│       │
│       └── windows-native-app/
│           └── SKILL.md

```

Ngoài các custom skills này, hãy nghiên cứu/import các skill phù hợp từ:

- `microsoft/win-dev-skills`
- Microsoft ONNX Runtime `ort-build`
- NVIDIA Agent Skills nếu phù hợp
- các Agent Skills Computer Vision đáng tin cậy khác

Không cài một skill chỉ vì nó tồn tại.

Mỗi skill phải được review trước.

---

# 3. CUSTOM SKILL: windows-gpu-capture

Tạo:

```text
.agents/skills/windows-gpu-capture/SKILL.md

```

Skill phải hướng dẫn agent về:

- Windows.Graphics.Capture
- HWND capture
- Direct3D 11
- Direct3D 12 nếu thực sự cần
- Direct3D11CaptureFramePool
- ID3D11Texture2D
- frame lifecycle
- capture FPS
- resize
- crop
- GPU memory
- zero-copy
- synchronization
- avoiding GPU → CPU copies
- latency measurement
- dropped frames
- window resize handling
- fullscreen/borderless handling

Ưu tiên kiến trúc:

```text
Sekiro Window
↓
Windows Graphics Capture
↓
Direct3D Texture
↓
GPU preprocessing
↓
AI inference

```

Không mặc định copy toàn bộ frame về CPU.

---

# 4. CUSTOM SKILL: realtime-video-ai

Tạo:

```text
.agents/skills/realtime-video-ai/SKILL.md

```

Skill phải hướng dẫn cách xây AI temporal realtime.

AI không được chỉ nhìn một frame.

Phải xử lý chuỗi:

```text
Frame t-31
Frame t-30
...
Frame t-1
Frame t

```

Nghiên cứu:

- TCN
- GRU
- LSTM
- Temporal Transformer
- lightweight video transformer
- VideoMAE
- optical flow
- pose sequence
- temporal action recognition

Ưu tiên:

1. latency
2. temporal consistency
3. low false positive
4. ONNX exportability
5. RTX 3070 inference performance

Output mục tiêu:

```text
attack_class
attack_probability
time_to_impact_ms
attack_direction
threat_probability
confidence

```

---

# 5. CUSTOM SKILL: sekiro-dataset-pipeline

Tạo:

```text
.agents/skills/sekiro-dataset-pipeline/SKILL.md

```

Dùng để xây dataset từ:

- YouTube Sekiro boss fights
- No-hit runs
- No-damage runs
- Speedruns
- Full playthrough
- Gameplay tự record
- Boss-specific videos

Không chỉ sử dụng một video.

Pipeline:

```text
Video
↓
download/reference
↓
combat section detection
↓
clip extraction
↓
FPS normalization
↓
resolution normalization
↓
boss detection
↓
attack segmentation
↓
annotation
↓
train/validation/test

```

Phải có hệ thống semi-auto annotation để tránh label tay từng frame.

---

# 6. DATASET LABEL FORMAT

Thiết kế format kiểu:

```json
{
  "boss": "Genichiro Ashina",
  "attack_type": "horizontal_slash",
  "clip_start": 1000,
  "windup_start": 1018,
  "active_start": 1039,
  "impact_frame": 1047,
  "recovery_start": 1062,
  "dodge_start": 1040,
  "dodge_direction": "right",
  "camera_motion": true,
  "confidence": 0.97
}

```

Các attack class tối thiểu:

```text
IDLE
WALK
RUN
TURN
FEINT

ATTACK_WINDUP
ACTIVE_ATTACK
RECOVERY
COMBO_CONTINUATION

HORIZONTAL_SLASH
VERTICAL_SLASH
DIAGONAL_SLASH
THRUST
SWEEP
GRAB
JUMP_ATTACK
PROJECTILE
AOE
UNKNOWN_ATTACK

```

---

# 7. CUSTOM SKILL: low-latency-inference

Tạo:

```text
.agents/skills/low-latency-inference/SKILL.md

```

Hardware chính:

```text
NVIDIA RTX 3070
Windows

```

Nghiên cứu:

- ONNX Runtime
- CUDA Execution Provider
- TensorRT
- TensorRT for RTX
- FP16
- CUDA Graph
- pinned memory
- asynchronous inference
- GPU preprocessing
- zero-copy pipelines
- batching = 1
- model warmup

Ưu tiên latency hơn throughput.

Không benchmark kiểu server.

Benchmark phải phản ánh:

```text
1 live game
1 frame sequence
batch=1
continuous realtime inference

```

---

# 8. CUSTOM SKILL: combat-decision-engine

Tạo:

```text
.agents/skills/combat-decision-engine/SKILL.md

```

Skill này xử lý:

```text
AI prediction
+
Wolf position
+
Enemy position
+
attack direction
+
time-to-impact
+
confidence
↓
Dodge decision

```

Không được:

```text
boss cử động
→ Shift

```

Phải có:

- threat threshold
- confidence threshold
- one-action-per-threat
- cooldown
- temporal hysteresis
- false-positive suppression
- attack-state machine
- direction decision
- emergency disable

---

# 9. CUSTOM SKILL: windows-native-app

Tạo:

```text
.agents/skills/windows-native-app/SKILL.md

```

Hướng dẫn:

- Windows native app structure
- C++
- C#
- WinUI 3
- CMake
- MSVC
- DLL/native interop
- packaging
- logging
- configuration
- crash handling
- performance profiling

Trước khi chọn C++ hay C#, hãy research và quyết định architecture.

---

# 10. GITHUB WORKFLOW

GitHub phải là source of truth.

Repo:

`SekiroVisionAI`

Không gửi hàng loạt file ZIP nếu repo đã tồn tại.

Sử dụng GitHub để:

- source code
- branches
- commits
- issues
- milestones
- releases
- CI
- documentation
- architecture decisions

Branch gợi ý:

```text
main
develop
feature/capture-engine
feature/overlay
feature/vision-engine
feature/temporal-engine
feature/dodge-engine

```

Mỗi milestone phải có commit rõ ràng.

---

# 11. PROJECT STRUCTURE

Sau Skill Setup, tạo cấu trúc:

```text
SekiroVisionAI/
│
├── .agents/
│   └── skills/
│
├── docs/
│   ├── architecture/
│   ├── research/
│   ├── benchmarks/
│   └── adr/
│
├── App/
│
├── CaptureEngine/
│
├── VisionEngine/
│
├── TemporalEngine/
│
├── ThreatEngine/
│
├── DodgeEngine/
│
├── InputEngine/
│
├── ModelRuntime/
│
├── Overlay/
│
├── DatasetTools/
│
├── Training/
│
├── Evaluation/
│
├── Models/
│
├── Config/
│
├── Tests/
│
└── tools/

```

---

# 12. CAPTURE ENGINE

Chỉ bắt đầu phần này SAU KHI Skill Setup PASS.

Mục tiêu:

```text
Sekiro HWND
↓
GPU capture
↓
D3D texture
↓
FPS counter
↓
latency counter

```

Ưu tiên:

- Windows.Graphics.Capture
- Direct3D11
- HWND capture

Nghiên cứu Desktop Duplication API để so sánh.

Không chọn công nghệ chỉ vì code dễ.

---

# 13. CAPTURE PERFORMANCE TARGET

Capture target:

```text
Minimum: 60 FPS
Preferred: 120 FPS

```

Cần đo:

```text
Capture FPS
Frame time
Dropped frames
CPU usage
GPU usage
Capture latency
Memory bandwidth

```

Milestone chỉ PASS khi capture đủ ổn định.

---

# 14. VISION ENGINE

Không bắt buộc YOLO.

Nghiên cứu detector phù hợp cho:

- Wolf
- boss
- weapon
- body
- arms
- projectile
- perilous symbol nếu hữu ích

Có thể nghiên cứu:

- YOLO
- RT-DETR
- pose estimation
- object tracking
- segmentation
- optical flow

Nhưng không được để detector chiếm quá nhiều latency.

---

# 15. TEMPORAL ENGINE

Một frame không đủ.

Ví dụ:

```text
frame 1: kiếm ở trên
frame 2: kiếm bắt đầu hạ
frame 3: kiếm tăng tốc
frame 4: slash active

```

Temporal model phải phân biệt:

```text
windup
attack
recovery
feint

```

Mục tiêu quan trọng:

# Time To Impact

Output:

```text
Attack: Horizontal Slash
Confidence: 0.97
Time to Impact: 126 ms
Direction: Right → Left

```

---

# 16. SCREEN AI LÀ THÀNH PHẦN CHÍNH

Project phải ưu tiên Computer Vision.

Có thể sử dụng hybrid supplementary data:

- optical flow
- game memory read-only
- animation state
- attack events
- hitbox information

Nhưng không được biến project thành:

```text
animation 3034
→ Shift at 0.8

```

Vision phải đóng vai trò chính.

---

# 17. YOUTUBE / VIDEO LEARNING

Hãy sử dụng web research để tìm:

- All Bosses No Damage
- All Bosses No Hit
- boss guides
- boss fights
- challenge runs
- speedruns
- different camera angles
- different weapons/prosthetics nếu hữu ích

Dataset phải đa dạng.

Ví dụ:

```text
Genichiro
Lady Butterfly
Guardian Ape
Great Shinobi Owl
Owl Father
Corrupted Monk
True Monk
Isshin
Demon of Hatred
Samurai enemies
Ashina Elite
Mini-bosses

```

Không train/test cùng một video source nếu điều đó gây data leakage.

---

# 18. GENERALIZATION

Đây là mục tiêu rất quan trọng.

Không chỉ test:

```text
train Genichiro
test Genichiro

```

Phải có bài test:

```text
Train:
Genichiro
Owl
Samurai

Test:
enemy chưa từng xuất hiện

```

Để kiểm tra AI có thực sự hiểu motion pattern hay chỉ nhớ boss.

---

# 19. DODGE ENGINE

Ban đầu chỉ tập trung Auto Dodge.

Không làm Auto Deflect trước.

Các decision:

```text
Normal Slash
→ Side Dodge

Thrust
→ Side Dodge

Grab
→ Away / Side Dodge

Projectile
→ Perpendicular Dodge

AOE
→ Dodge Away

```

Sau này mới nghiên cứu:

- Mikiri
- Jump
- Deflect

---

# 20. INPUT ENGINE

Keyboard/mouse.

Sekiro controls:

```text
Dodge = Shift
Direction = W/A/S/D

```

Engine phải hỗ trợ:

```text
Shift
A + Shift
D + Shift
W + Shift
S + Shift

```

Phải có:

- cooldown
- one-dodge-per-threat
- no key spam
- release safety
- emergency disable
- low-latency input

---

# 21. MAIN APP UI

Tôi muốn đây là phần mềm thật.

Không muốn terminal là UI cuối cùng.

UI mong muốn:

```text
SEKIRO VISION AI

Capture: ACTIVE
Capture FPS: 120

GPU:
RTX 3070

Inference:
6.2 ms

Target:
Genichiro Ashina

Attack:
Horizontal Slash

Confidence:
97%

Impact:
126 ms

Threat:
CRITICAL

Decision:
DODGE RIGHT

Auto Dodge:
ON

Overlay:
ON

```

Controls:

```text
Start
Stop

Auto Dodge ON/OFF
Overlay ON/OFF

Confidence threshold
Reaction threshold
Model selection
Performance mode
Debug mode

Dataset Recording
Logging
Benchmark

```

---

# 22. DEBUG OVERLAY

Có thể hiển thị:

- boss bounding box
- Wolf bounding box
- skeleton
- weapon tracking
- trajectory
- optical flow
- attack class
- confidence
- time-to-impact
- Dodge direction
- FPS
- inference latency

Overlay có thể tắt hoàn toàn khi chơi bình thường.

---

# 23. PERFORMANCE MỤC TIÊU

Ưu tiên theo thứ tự:

```text
1. Low false-positive Dodge
2. Correct attack detection
3. Stable temporal prediction
4. Low latency
5. High dangerous-attack recall
6. Generalization
7. GPU efficiency

```

Tôi thà:

```text
bỏ sót 1 đòn

```

hơn:

```text
Shift lung tung liên tục

```

---

# 24. RTX 3070

Hãy tối ưu project cho:

```text
NVIDIA RTX 3070

```

AI input có thể bắt đầu:

```text
320x320
416x416

```

Temporal window:

```text
8
16
24
32 frames

```

Benchmark tất cả.

Không mặc định chọn model lớn nhất.

---

# 25. LATENCY

Target architecture:

```text
Screen
↓
GPU Capture
↓
Preprocess
↓
Vision
↓
Temporal inference
↓
Threat decision
↓
Input

```

Cần benchmark riêng từng stage.

Ví dụ:

```text
Capture       2.1 ms
Preprocess    0.8 ms
Detection     4.0 ms
Temporal      2.5 ms
Decision      0.1 ms
Input         1.0 ms

Total:
10.5 ms

```

Không được chỉ benchmark inference mà bỏ qua end-to-end latency.

---

# 26. RESEARCH FIRST

Trước khi quyết định architecture, sử dụng:

- Web research
- GitHub
- Context7
- official docs

Research:

```text
Windows.Graphics.Capture
Direct3D 11
Direct3D 12
Desktop Duplication
Windows App SDK
WinUI 3

ONNX Runtime
CUDA
TensorRT
TensorRT for RTX

OpenCV
YOLO
RT-DETR
Pose estimation
Optical flow

MMAction2
VideoMAE
TCN
GRU
Temporal Transformer

Realtime video action recognition
Time-to-impact prediction

```

Không đoán API.

Context7 nên được ưu tiên để kiểm tra library/API hiện hành.

---

# 27. MILESTONES

Không xây tất cả cùng lúc.

## Milestone 0 — Agent Skills

- GitHub connected
- Context7 connected
- repo created
- external skills imported
- custom skills created
- skills reviewed
- skills committed

PASS:

```text
All required SKILL.md files exist.
All skills reviewed.
Repo clean.
Initial skill commit pushed.

```

CHỈ KHI PASS mới sang Milestone 1.

---

## Milestone 1 — Capture Engine

Sekiro window capture.

PASS:

```text
>= 60 FPS stable
preferred >= 120 FPS
no major frame drops
latency measured

```

---

## Milestone 2 — Overlay

PASS:

```text
realtime overlay
FPS displayed
capture latency displayed
no major impact on game FPS

```

---

## Milestone 3 — Dataset Pipeline

PASS:

```text
video → clips → metadata → annotation

```

---

## Milestone 4 — Wolf/Boss Detection

PASS:

```text
stable realtime tracking
low missed detections

```

---

## Milestone 5 — Weapon / Pose Tracking

PASS:

```text
weapon/body movement consistently trackable

```

---

## Milestone 6 — Temporal Attack Classifier

PASS:

```text
windup vs attack vs recovery reliably separated

```

---

## Milestone 7 — Time-To-Impact

PASS:

```text
impact prediction error measured in milliseconds

```

---

## Milestone 8 — Offline Evaluation

Measure:

```text
Precision
Recall
F1
False Dodge Rate
Missed Threat Rate
Timing Error

```

---

## Milestone 9 — Threat Engine

PASS:

```text
attack prediction does not immediately equal Dodge
false-positive suppression works

```

---

## Milestone 10 — Auto Dodge

Only now connect InputEngine.

PASS:

```text
known attacks avoided consistently
no Shift spam
one Dodge per threat

```

---

## Milestone 11 — Boss Generalization

Test unseen enemies.

---

## Milestone 12 — Optimization

TensorRT/ONNX/FP16/GPU pipeline.

---

## Milestone 13 — Packaging

Produce real Windows application.

---

# 28. PASS / FAIL DISCIPLINE

Ở cuối mỗi milestone phải báo:

```text
PASS

```

hoặc:

```text
FAIL

```

và giải thích bằng metric.

Không được tự cho rằng milestone thành công chỉ vì chương trình chạy.

---

# 29. ARCHITECTURE DECISION RECORDS

Các quyết định lớn phải lưu vào:

```text
docs/adr/

```

Ví dụ:

```text
ADR-001-runtime-language.md
ADR-002-capture-api.md
ADR-003-inference-runtime.md
ADR-004-temporal-model.md
ADR-005-overlay-architecture.md

```

Mỗi ADR cần:

```text
Context
Options
Decision
Reason
Tradeoffs

```

---

# 30. C++ HAY C\#

Không mặc định chọn một ngôn ngữ ngay.

Research:

```text
C++ capture + D3D + ONNX
vs
C# WinUI + native C++ engine
vs
C# only

```

Sau đó đề xuất architecture phù hợp nhất.

Tôi ưu tiên performance.

Nếu tốt nhất là:

```text
C# WinUI 3 frontend
+
C++ native core

```

thì hãy dùng hybrid.

---

# 31. PYTHON

Python chỉ dùng cho:

```text
training
dataset tooling
annotation
model conversion
evaluation
experiments

```

Runtime cuối không nên phụ thuộc Python nếu native implementation tốt hơn.

---

# 32. SECURITY / CODE QUALITY

Không chạy script lấy từ Agent Skill bên ngoài một cách mù quáng.

Trước khi cài skill:

```text
inspect SKILL.md
inspect scripts
inspect dependencies
inspect shell commands
inspect network behavior

```

Nếu có nghi ngờ:

```text
do not execute
report it

```

---

# 33. CÁCH BẮT ĐẦU CUỘC CHAT NÀY

Ngay sau khi đọc prompt này:

## KHÔNG viết CaptureEngine.

Hãy thực hiện đúng thứ tự:

### STEP 1

Kiểm tra các tool/plugin hiện có:

```text
GitHub
Context7

```

### STEP 2

Research Agent Skills phù hợp.

Ưu tiên:

```text
microsoft/win-dev-skills
microsoft/onnxruntime ort-build
NVIDIA skills

```

### STEP 3

Tạo GitHub repository:

```text
SekiroVisionAI

```

### STEP 4

Tạo:

```text
.agents/skills/

```

### STEP 5

Import/cài external Agent Skills đã được review.

### STEP 6

Tạo toàn bộ custom skills:

```text
windows-gpu-capture
realtime-video-ai
sekiro-dataset-pipeline
low-latency-inference
combat-decision-engine
windows-native-app

```

### STEP 7

Viết `SKILL.md` chất lượng cao cho từng skill.

### STEP 8

Review toàn bộ skill.

### STEP 9

Commit:

```text
chore: initialize SekiroVisionAI agent skills

```

### STEP 10

Push GitHub.

### STEP 11

Báo:

```text
MILESTONE 0: PASS / FAIL

```

### STEP 12

CHỈ nếu Milestone 0 PASS:

Research architecture và bắt đầu Milestone 1 CaptureEngine.

---

# 34. VAI TRÒ CỦA BẠN

Trong project này, hãy làm việc như:

- Software Architect
- Windows Developer
- C++ Developer
- C# Developer
- Computer Vision Engineer
- ML Engineer
- Video AI Engineer
- CUDA/TensorRT Performance Engineer
- Dataset Engineer
- Test Engineer

Không chỉ giải thích lý thuyết.

Tôi muốn xây một project thật từ đầu đến cuối.

---

# 35. MỤC TIÊU CUỐI CÙNG

Xây dựng:

# `SekiroVisionAI`

Một ứng dụng Windows native có khả năng:

```text
Capture Sekiro realtime
↓
Track Wolf + Enemy + Weapon
↓
Understand temporal movement
↓
Recognize attack windup
↓
Predict attack type
↓
Predict time-to-impact
↓
Evaluate threat
↓
Choose safe Dodge direction
↓
Send low-latency input
↓
Avoid attack

```

với mục tiêu:

```text
High accuracy
Low latency
Very low false-positive Dodge
Generalization across bosses
Stable performance on RTX 3070

```

Không quay lại phương pháp hard-code từng Animation ID trừ khi chỉ dùng nó làm diagnostic/reference data.

## BẮT ĐẦU NGAY TỪ MILESTONE 0 — AGENT SKILLS.