SekiroVisionAI - ban phat trien noi bo

TRANG THAI
Chua phai ban Auto Dodge hoan chinh. Xem build-manifest.json va
Models/production/manifest.json de biet model thuc su da duoc dong goi hay chua.
Khong dung model synthetic/contract-test de cho phep Auto Dodge.

KHOI DONG UNG DUNG WINDOWS
1. Giai nen toan bo thu muc, giu cac DLL canh SekiroVisionAI.exe.
2. Mo Sekiro. Mo SekiroVisionAI.exe bang double-click.
3. Bam Start. Ung dung tu tim sekiro.exe va tao ROI tu dong.
4. Khi model du dieu kien, bam Auto Dodge roi quay ve game, hoac bam F8 trong game.
5. F8 bat/tat. F9 tat khan cap va nha cac phim do ung dung dang giu.
6. Stop dung capture va Auto Dodge. Alt-tab/focus loss cung tat Auto Dodge.

Khong can Python, terminal, Visual Studio, CMake hay CUDA Toolkit de khoi dong.
Runtime DLL da kem theo. Khong xoa/le ra rieng file EXE.
Full quality AI frame va preview mau co duong rieng; provider va frame age
duoc hien theo so do thuc te. Debug cho phep xem chi tiet, ROI thu cong,
model khac va heuristic CV. Heuristic khong phai temporal AI da train.

LOG
Log JSONL tai %LocalAppData%/SekiroVisionAI/logs.
THREAT_STATE / THREAT_READY: trang thai va quyet dinh threat.
DODGE_REQUEST / DODGE_SENT: yeu cau va input da duoc Windows chap nhan.
SendInput thanh cong chua phai bang chung Sekiro da ne thanh cong.

KIEM THU NOI BO
ReplayHarness.exe dung cung native CombatPipeline voi ung dung.
Replay khong gui phim Windows. Trace ghi SIMULATED_INPUT_ACCEPTED.
Huong dan day du trong ReplayHarness/README.md tren repository.

GIOI HAN CHUA DUOC XAC NHAN
Chua co ket qua tren RTX 3070 khi Sekiro dang render; khong suy ra tu CI.
Theo doi chuyen dong tam thoi khong phai nhan dien semantic Wolf/boss.
Bao phu boss, TTI va false Dodge phai duoc do tren gameplay co nhan phu hop.
