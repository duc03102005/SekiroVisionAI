SekiroVisionAI — bản phát triển nội bộ

TRẠNG THÁI
Chưa phải bản Auto Dodge hoàn chỉnh. Gói hiện tại chưa kèm model production,
nên Auto Dodge bằng temporal AI chưa khả dụng trong chế độ thông thường.
Các model train từ video thật vẫn là ứng viên nghiên cứu: chưa hỗ trợ
threat/TTI và chưa nhận diện boss đủ tốt. Model synthetic không được phép
kích hoạt Auto Dodge.

KHỞI ĐỘNG ỨNG DỤNG WINDOWS
1. Giải nén toàn bộ thư mục, giữ các DLL cạnh SekiroVisionAI.exe.
2. Mở Sekiro rồi mở SekiroVisionAI.exe bằng double-click.
3. Bấm Start. Ứng dụng tự tìm sekiro.exe và bắt đầu capture.
4. Với model đủ điều kiện, bấm Auto Dodge rồi quay lại game, hoặc bấm F8.
5. F8 bật/tắt Auto Dodge. F9 tắt khẩn cấp và nhả phím do ứng dụng giữ.
6. Stop dừng capture và Auto Dodge. Mất focus game cũng tắt Auto Dodge.

Không cần Python, terminal, Visual Studio, CMake hay CUDA Toolkit để mở app.
Runtime DLL đã kèm theo; không tách riêng EXE khỏi thư mục.
Mỗi lần mở, app chọn model quản lý tại Models/production cạnh EXE hiện tại,
provider Auto và ROI tự động. Cài đặt model/ROI/CV trong Advanced/Debug chỉ
áp dụng cho phiên hiện tại. App luôn khởi động với Auto Dodge OFF.

Frame màu ở độ phân giải nguồn dành cho AI được giữ riêng với preview nhỏ.
Provider và frame age hiển thị theo số đo thực tế. Chế độ CV heuristic nằm
trong Advanced/Debug và được ghi rõ trên màn hình; điểm CV không phải xác
suất từ model AI. Hiện chưa có bằng chứng tự né thành công trong Sekiro.

LOG
Log JSONL tại %LocalAppData%/SekiroVisionAI/logs.
THREAT_STATE / THREAT_READY: trạng thái và quyết định threat.
DODGE_REQUEST / DODGE_SENT: yêu cầu và input đã được Windows chấp nhận.
SendInput thành công chưa chứng minh nhân vật đã né tránh đòn đánh.

REPLAY NỘI BỘ
ReplayHarness.exe dùng cùng native CombatPipeline với ứng dụng.
Replay không gửi phím Windows; trace ghi SIMULATED_INPUT_ACCEPTED.
Số frame đã đọc, số inference thực sự chạy và số frame xác nhận mục tiêu
được ghi riêng. Replay với ROI Debug không chứng minh ROI tự động hoạt động.
Hướng dẫn kỹ thuật: ReplayHarness/README.md trong repository.

GIỚI HẠN CÒN LẠI
Chưa có model threat/TTI, detector đủ rộng cho các boss hoặc số đo false
Dodge trên gameplay độc lập. Chưa có phép đo khi Sekiro đang render trên
RTX 3070. Đường capture AI hiện còn readback qua CPU; chưa phải zero-copy.
Xem build-manifest.json và Models/production/manifest.json trong gói để biết
chính xác trạng thái build và model đi kèm.
