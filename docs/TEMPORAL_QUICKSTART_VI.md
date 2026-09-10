# SekiroVisionAI 0.3 — Windows Auto Dodge + temporal AI pipeline

## Bản này có gì?

Preview màu rõ hơn, capture Sekiro, load model ONNX temporal, xem attack/threat/TTI,
quyết định né theo TTI, gửi Shift hoặc direction+Shift, và ghi mẫu để review/train.
Dataset tools và ba kiến trúc training nằm trong cùng repo.

**Chưa kèm model gameplay đã train.** Catalog60 nguồn là danh sách nghiên cứu;
chưa tải được footage có quyền sử dụng được xác minh và nhãn gameplay đã review.
Model tạo từ fixture tổng hợp chỉ dùng kiểm tra, không bật được Auto Dodge AI.
Bản này chưa chứng minh né boss thành công trên máy người dùng.

## Chạy Windows app

1. Tải ZIP từ GitHub Actions của nhánh `feature/temporal-ai-pipeline`, artifact
   `SekiroAutoDodge-Temporal-Windows-x64-<commit>`. Giải nén toàn bộ, giữ các DLL
   cạnh `SekiroAutoDodge.exe`. Không lấy artifact `SYNTHETIC-ONLY` làm AI gameplay.
2. Mở Sekiro ở Windowed/Borderless, Dodge vẫn gán **Shift**. Mở exe, chọn cửa sổ
   Sekiro, nhấn **Start capture**. Preview màu phải hiện đúng game.
3. Lock on rồi kéo ROI bao enemy/body/weapon, hạn chế Wolf/HUD. Camera đổi nhiều
   có thể cần chỉnh ROI lại. Đây vẫn là ROI thủ công.
4. Có model đã train: **Load ONNX model...**, chọn bundle `model.onnx`, chọn
   DirectML GPU hoặc CPU. Model load/sampling/threat/TTI xuất hiện ở bên phải.
   Model có attack head nhưng thiếu threat/TTI vẫn xem được score; chưa gửi né AI.
5. Muốn dùng lại đường Dodge hoạt động của MVP trong lúc chưa có model: chọn
   **CV heuristic fallback**. Các score lúc này là heuristic, không phải AI đã train.
6. Quay lại game, nhấn **F8** để bật. Chờ history và một khoảng yên trước đòn mới.
   App chỉ gửi né khi bộ detector đang chọn đáp ứng điều kiện. **F9** tắt ngay.

Không cần Python để chạy exe. CPU/DirectML được đóng gói sẵn; GPU cần driver
Windows hoạt động. Bản ký số chưa có. Cửa sổ game mất focus, capture cũ, Stop,
lỗi model/input hoặc watchdog sẽ tắt Auto Dodge; quay lại game và F8 để bật lại.
Nếu game chạy elevated mà app không elevated, Windows có thể từ chối SendInput;
dùng cùng mức quyền thông thường cho game và app.

## Hotkey

| Phím | Chức năng |
|---|---|
| F8 | Bật/tắt Auto Dodge khi Sekiro foreground và detector sẵn sàng |
| F9 | Tắt + nhả phím tổng hợp đang giữ |
| F10 | Một lần thử gửi Dodge khi Auto Dodge đang bật |
| F7 | Bật/tắt Record Training Samples trong game |
| F6 | Lưu mẫu review với1s trước/sau |
| F5 | Đánh dấu nghi false positive để review |
| F4 | Đánh dấu nghi missed attack để review |

F4–F7 chỉ xử lý marker khi cửa sổ Sekiro được chọn đang foreground. Nếu ứng dụng
khác đã giữ hotkey, log báo conflict. F10 xác minh đường input, không chứng minh
detector tự nhận đòn hoặc né thành công. Giữ W/A/S/D thật sẽ ưu tiên hướng người
chơi; app không đoán rằng hướng model quan sát là hướng né an toàn.

## Nếu F8 không Dodge

Xem dòng Auto Dodge và log **F8_BLOCKED**, không chỉ FPS:

- `NO_MODEL_FILE`/`MODEL_NOT_GAMEPLAY_TRAINED`: chưa có model phù hợp; load model
  đã train hoặc chọn rõ CV fallback để dùng đường MVP.
- `UNSUPPORTED_ATTACK_THREAT_TTI_HEADS_OR_MODEL_NO_AUTO`: nhãn/head chưa hỗ trợ
  tự né; các score có thể chỉ phục vụ quan sát.
- `RETURN_TO_SEKIRO_THEN_F8`: Alt-tab về game rồi nhấn F8.
- `START_CAPTURE_FIRST`/`CAPTURE_TOO_OLD_FOR_F8`: kiểm tra preview/capture trước.
- `WAIT_QUIET_BEFORE_ARMING`, `INSUFFICIENT_DWELL`, `LOW_CONFIDENCE_OR_THREAT`,
  `TTI_TOO_WIDE`, `WAIT_TTI_WINDOW`, `TOO_LATE`: detector có lý do cụ thể chưa gửi.
- `DODGE_SENT` có nhưng game không né: kiểm tra Shift binding, cooldown/state
  trong game, focus và mức quyền. SendInput thành công là Windows nhận sự kiện,
  không phải xác nhận game đã thực hiện động tác.

Thông số model ban đầu: attack/threat0.8, dwell33ms, quiet180ms, cooldown450ms,
cửa sổ lead70–130ms, uncertainty scale tối đa25ms. Đây là điểm xuất phát có thể
chỉnh trong `mvp.ini`, chưa phải timing chuẩn cho tất cả boss. Điểm TTI và Laplace
scale từ model không phải khoảng tin cậy đã hiệu chuẩn. Không tự né sweep/AOE
trong policy hiện tại. Heuristic có preset riêng Conservative/Balanced/Responsive.

## Log và mẫu training

**Config / logs folder** mở `%LocalAppData%\SekiroVisionAI`.
Log JSONL ghi `MODEL_LOADED`, `MODEL_SIGNALS`, `THREAT_READY`, `DODGE_REQUEST`,
`DODGE_SENT`, `KEY_UP_SUBMITTED`, `DODGE_SUPPRESSED`, `AUTO_DISABLED`; có model,
frame sequence/source timestamp, class, score, TTI/uncertainty và lý do quyết định.
Boss/distance còn unknown. Probability/TTI chưa có sẽ không được hiển thị giả.

Bật **Record Training Samples** bằng checkbox hoặc F7. Khi Windows nhận một Dodge,
recorder giữ1s trước/1s sau. F4/F5/F6 tạo marker thủ công. Dữ liệu nằm trong
`training_samples/<session>/`: JPEG màu + `sample.json` + `frames.ffconcat`.
Đây là frame bundle, chưa phải MP4; hướng dẫn đổi sang MP4 trong
`docs/recording-format.md`. Tắt/Stop/gap có thể lưu mẫu thiếu phần sau với
`truncated=true`. Giới hạn2 mẫu đang xử lý và2GiB mỗi lần chạy; đầy thì drop có log.
Các marker đều UNREVIEWED, không tự biến thành nhãn đòn/impact.

## Dataset, annotation, training

Đọc `DatasetTools/README.md` và `Training/README.md` trong repo. Pipeline gồm:
ingest footage hợp lệ → mine clip ngắn → review annotation → chia theo nguồn/
người chơi/session/boss → train → export ONNX → Load ONNX model trong app.
Không có điều kiện phải đủ tất cả boss hoặc benchmark xong mới train/tích hợp.
No-hit không tự cung cấp impact ground truth; threat/TTI thiếu bằng chứng được mask.

Build từ source: `cmake --preset windows-x64`,
`cmake --build --preset windows-release --parallel`.
Mặc định dùng ONNX Runtime DirectML1.24.4 + DirectML1.15.4. Có tùy chọn CMake
`-DSVAI_ORT_FLAVOR=CPU` hoặc `CUDA`; CUDA1.25.0 cần CUDA/cuDNN tương thích và chưa
được đo với Sekiro/RTX3070 ở đây. Exe của artifact mặc định là bản DirectML/CPU.
