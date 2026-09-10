# SekiroVisionAI — Auto Dodge MVP 0.2

Bản này đã nối **capture Sekiro → ROI → CV/chuyển động → temporal threat → SendInput Shift hoặc hướng + Shift**. Không cần model đã train, Python, OpenCV hay Visual Studio để chạy file EXE. Detector hiện là heuristic thị giác; chưa có số đo độ chính xác trong game và không bảo đảm né được mọi đòn.

## Chạy nhanh

1. Tải artifact **SekiroAutoDodge-MVP-Windows-x64-…** từ lượt GitHub Actions thành công. Giải nén toàn bộ ZIP ra một thư mục. Mở **SekiroAutoDodge.exe**. Trong gói có hướng dẫn này và `build-manifest.json` ghi commit cùng SHA-256 của EXE.
2. Mở Sekiro trên Windows x64, dùng **Windowed hoặc Borderless**, tắt HDR cho bản đầu. Kiểm tra phím Dodge/Step Dodge trong game là **Left Shift**. App không tự sửa keybind của game.
3. Trong app, bấm **Refresh**, chọn cửa sổ `sekiro.exe`, rồi **Start capture**. Ảnh game phải xuất hiện trong ô xem trước. Auto Dodge vẫn OFF.
4. Lock-on một enemy ở khoảng cách giao chiến. Kéo một hình chữ nhật trong ảnh xem trước để bao vùng thân/tay/vũ khí của enemy. Tránh đưa Wolf, HUD, thanh máu, quá nhiều nền hay vật đang chuyển động vào ROI. Khung xanh là ROI được dùng thật. Nếu khung không phù hợp, chọn **Reset ROI** rồi kéo lại.
5. Chọn **Balanced** để bắt đầu và **Shift / player direction**. Quay lại cửa sổ game, nhấn **F8**. App phát tiếng báo khi bật; cần khoảng một giây hình ảnh tương đối yên để thiết lập lịch sử trước khi nhận một đợt chuyển động mới.
6. Cho enemy tấn công. Khi tín hiệu trong ROI đạt ngưỡng và đủ thời gian arming, app gửi Dodge thật. Nhấn **F9** bất cứ lúc nào để tắt và nhả phím do app giữ.

Không cần chạy benchmark trước khi dùng MVP. Bắt đầu với một enemy và góc camera dễ giữ ổn định, sau đó chỉnh ROI/ngưỡng dựa trên log.

## Hotkey và điều khiển

| Điều khiển | Tác dụng |
|---|---|
| **F8** | Bật/tắt Auto Dodge; chỉ bật khi cửa sổ Sekiro được chọn đang foreground và có capture mới |
| **F9** | Tắt Auto Dodge ngay và yêu cầu nhả mọi phím do app đang giữ |
| **F10** | Gửi **một Dodge thủ công để thử đường input**, khi Auto Dodge đang ON và Sekiro foreground; vẫn chịu cooldown và kiểm tra phím |
| Start capture / Stop capture | Bắt đầu/dừng capture; Stop đồng thời tắt input |
| Disable (F9) | Tắt input bằng nút trong app |
| Kéo ROI / đổi preset / đổi hướng | Áp dụng cấu hình và tắt Auto Dodge; quay lại game, nhấn F8 để bật lại |
| Open log | Mở log phiên hiện tại trong Notepad |
| Config / logs folder | Mở thư mục cấu hình và log của app |

F10 là phép thử phím, không phải bằng chứng detector đã nhận ra một đòn. Hãy nhìn `THREAT_READY` trước `DODGE_SENT` để phân biệt Dodge tự động. `MANUAL_DODGE_REQUEST` là lần thử bằng F10.

**Alt-tab, minimize, capture lỗi hoặc frame quá cũ sẽ tắt Auto Dodge.** Bật lại bằng F8 sau khi game/capture sẵn sàng. Nếu capture dừng vì minimize, đổi monitor hoặc game đóng, bấm Stop/Start hoặc Refresh và chọn lại cửa sổ. Sau resize, chờ ảnh mới rồi F8. Chỉ chạy một instance app để tránh trùng hotkey.

## Giảm Dodge loạn hoặc tăng khả năng bắt chuyển động

| Preset | Entry / exit | Quality tối thiểu | Arming | Cooldown |
|---|---|---|---|---|
| Conservative | 0.72 / 0.48 | 0.62 | 55 ms | 800 ms |
| Balanced | 0.62 / 0.40 | 0.55 | 40 ms | 650 ms |
| Responsive | 0.52 / 0.34 | 0.50 | 30 ms | 650 ms |

Các số này là **tham số khởi đầu chưa được hiệu chuẩn bằng dữ liệu Sekiro**, không phải phần trăm chính xác của AI. Responsive dễ phản ứng hơn và cũng có thể né nhầm nhiều hơn. Trước khi hạ ngưỡng, kiểm tra ROI có bao đúng chuyển động của enemy và có loại Wolf/nền/HUD chưa.

Detector kết hợp chênh lệch frame sau khi bù dịch chuyển camera, tỷ lệ pixel thay đổi, sparse block flow và gia tốc chuyển động. Nó cần một đợt chuyển động nổi bật tại ROI, duy trì qua nhiều frame. Sau một lần trigger, episode bị đánh dấu đã dùng; hết cooldown **chưa đủ** để né tiếp — phải thấy khoảng yên tối thiểu 220 ms. Do đó một combo không có khoảng yên có thể chỉ được né một lần ở MVP.

Các lựa chọn hướng là hướng phím bạn chọn, **không phải hướng an toàn do AI tính**. Mặc định chỉ Shift, giữ hướng bạn đang điều khiển. Nếu chọn A/D/S/W + Shift mà bạn đang giữ một phím WASD, app chỉ thêm Shift để tránh đối kháng với hướng của bạn. Nếu Shift/Ctrl/Alt/Win đang được giữ, app bỏ lần gửi đó.

## Đọc log

Log được ghi tự động vào `%LOCALAPPDATA%\SekiroVisionAI\logs\autododge-*.jsonl`. Mỗi dòng là JSON, có QPC time, loại event, episode và chi tiết. Không lưu ảnh/video game. Log tín hiệu khoảng 5 lần/giây; các sự kiện gửi/nhả phím được ghi riêng. UI giữ 60 sự kiện gần nhất; hàng đợi ghi có giới hạn. Một phiên xoay log khi tệp đạt khoảng 8 MiB; các phiên cũ vẫn nằm trong thư mục để bạn chủ động lưu/xóa.

| Event / lý do | Ý nghĩa |
|---|---|
| `CV_SIGNALS` | Score, quality, changed fraction, flow, gia tốc, dịch camera, tuổi frame và thời gian CV |
| `THREAT_STATE` / `ATTACK_ARMING` | Bắt đầu tích lũy tín hiệu ứng viên |
| `THREAT_READY` | Heuristic đã tạo một threat đủ điều kiện cho temporal detector |
| `DODGE_REQUEST` | Yêu cầu input cho threat đó |
| `DODGE_SENT` | SendInput báo đã chèn đủ key-down yêu cầu vào luồng input Windows; không chứng minh game né thành công |
| `KEY_UP_SUBMITTED` | Windows đã nhận key-up của các phím app sở hữu |
| `DODGE_SUPPRESSED` | Không gửi phím; đọc lý do kèm theo |
| `ONE_DODGE_PER_EPISODE` | Episode đã dùng; đang chờ khoảng yên để nhận đợt mới |
| `CAMERA_ONLY` / `CAMERA_CUT_OR_FLASH` | Chuyển động nền, camera nhanh hoặc thay đổi sáng bị loại |
| `WAIT_FOR_QUIET` / `TEMPORAL_WARMUP` | Chưa đủ lịch sử hoặc chưa thấy khoảng yên |
| `PHYSICAL_KEY_CONFLICT` | Có Shift/modifier đang giữ hoặc một chord chưa nhả |
| `SENDINPUT_FAILED` | Xem `inserted`, `requested`, mã Win32; Auto Dodge bị tắt |
| `WATCHDOG_RELEASE` / `INPUT_RELEASE_FAILED` | Có vấn đề ở thời hạn/nhả phím; app tắt input. Khởi động lại app sau khi kiểm tra |

App có luồng input/hotkey riêng và một tiến trình watchdog cùng EXE. Shift thường được giữ **45 ms** rồi nhả. Watchdog cố nhả phím sở hữu nếu tiến trình cha chết hoặc vượt thời hạn **250 ms**, với số lần thử giới hạn. Đây là xử lý best effort của Windows; không có cam kết tuyệt đối nếu OS từ chối input/desktop bị ngắt. Nếu thấy lỗi nhả phím, nhấn rồi thả Shift/WASD vật lý và khởi động lại app.

## Nếu không thấy Dodge

1. **Ảnh không hiện:** xem trạng thái Capture và HRESULT. Dùng Borderless/Windowed, để game trên một monitor, thử Refresh/Start. Bản này không tự chuyển sang Desktop Duplication hay software capture.
2. **F8 không bật:** quay lại foreground của Sekiro, kiểm tra frame mới; đóng instance app trùng hoặc ứng dụng khác chiếm F8/F9/F10. Nếu Watchdog unavailable, mở lại app và kiểm tra thông báo Windows.
3. **F10 cũng không né:** kiểm tra keybind Left Shift trong game và log `DODGE_SENT`/`SENDINPUT_FAILED`; thả Shift cùng modifier đang giữ. App và game cần cùng mức quyền Windows để SendInput hoạt động; ưu tiên chạy cả hai bình thường. Không có cơ chế vượt UIPI.
4. **F10 né nhưng tự động không né:** đường input đã hoạt động. Kiểm tra ROI, score/quality, `WAIT_FOR_QUIET`, camera motion và preset; thử Responsive nếu chuyển động enemy chưa vượt ngưỡng.
5. **Né quá nhiều:** nhấn F9, thu ROI quanh enemy/tay/vũ khí, loại Wolf/nền rồi chọn Conservative. Di chuyển camera, hiệu ứng, đi bộ của enemy hoặc người chơi vẫn có thể gây nhầm ở detector heuristic này.

## Cấu hình nâng cao

Cấu hình nằm tại `%LOCALAPPDATA%\SekiroVisionAI\mvp.ini`. App tự tạo và lưu ROI/preset/hướng. Đóng app trước khi sửa thủ công rồi mở lại. ROI dùng tọa độ trên ảnh từ 0–1000; hướng `0=Shift`, `1=A`, `2=D`, `3=S`, `4=W`. Có thể sửa `enter_percent`, `exit_percent`, `confidence_percent`, `arming_ms`, `cooldown_ms`, `quiet_ms`, `hold_ms` trong giới hạn được app kiểm tra. Giữ `exit_percent` thấp hơn `enter_percent` để có hysteresis.

Khi cần cải thiện detector, gửi log phiên, cấu hình `mvp.ini`, enemy/góc camera đã thử và đoạn video tương ứng nếu có. Không cần hoàn thành benchmark M1. Dữ liệu này sẽ giúp giảm false Dodge và thay heuristic bằng model được train sau.
