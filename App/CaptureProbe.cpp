#include <SekiroVisionAI/CaptureEngine.h>
#include <commdlg.h>
#include <psapi.h>
#include <array>
#include <iomanip>
#include <sstream>

namespace {
using namespace sekiro;
constexpr int id_windows = 101, id_refresh = 102, id_start = 103, id_stop = 104, id_record = 105, id_export = 106;

std::wstring number(double value, int precision = 2) {
    if (!std::isfinite(value)) return L"N/A";
    std::wostringstream out; out << std::fixed << std::setprecision(precision) << value; return out.str();
}

class Probe {
public:
    CaptureEngine engine;
    HWND window{}, heading{}, targets_box{}, refresh{}, start{}, stop{}, record{}, export_button{}, stats{};
    HFONT font{};
    UINT dpi{96};
    bool closing{};
    std::vector<WindowTarget> targets;
    std::uint64_t previous_cpu{};
    double previous_wall{}, cpu_percent{unavailable};

    ~Probe() { if (font) DeleteObject(font); }
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int id) {
        return CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    }
    void create(HWND hwnd) {
        window = hwnd; dpi = GetDpiForWindow(window);
        heading = control(L"STATIC", L"SekiroVisionAI  |  M1 capture diagnostics\r\nSelect the game window. Capture does not send keyboard input.", 0, 0);
        targets_box = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id_windows);
        refresh = control(L"BUTTON", L"Refresh", WS_TABSTOP, id_refresh);
        start = control(L"BUTTON", L"Start", WS_TABSTOP | BS_DEFPUSHBUTTON, id_start);
        stop = control(L"BUTTON", L"Stop", WS_TABSTOP, id_stop);
        record = control(L"BUTTON", L"Record timing trace (up to 120,000 frames)", BS_AUTOCHECKBOX | WS_TABSTOP, id_record);
        export_button = control(L"BUTTON", L"Export trace...", WS_TABSTOP, id_export);
        stats = control(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 0);
        if (!heading || !targets_box || !refresh || !start || !stop || !record || !export_button || !stats)
            throw std::runtime_error("Could not create diagnostic controls.");
        apply_font(); layout(); refresh_windows(); update();
        SetTimer(window, 1, 250, nullptr);
    }
    void apply_font() {
        HFONT next = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        for (auto h : {heading, targets_box, refresh, start, stop, record, export_button, stats})
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
        if (font) DeleteObject(font);
        font = next;
    }
    void layout() {
        RECT rect{}; GetClientRect(window, &rect);
        auto move = [&](HWND h, int x, int y, int width, int height) { MoveWindow(h, px(x), px(y), px(width), px(height), TRUE); };
        const int width = MulDiv(rect.right, 96, static_cast<int>(dpi));
        const int height = MulDiv(rect.bottom, 96, static_cast<int>(dpi));
        move(heading, 20, 16, width - 40, 48);
        move(targets_box, 20, 72, width - 150, 220); move(refresh, width - 120, 71, 100, 30);
        move(start, 20, 115, 90, 32); move(stop, 120, 115, 90, 32);
        move(record, 226, 116, width - 382, 30); move(export_button, width - 148, 115, 128, 32);
        move(stats, 20, 168, width - 40, std::max(80, height - 188));
    }
    void refresh_windows() {
        targets = find_sekiro_windows();
        SendMessageW(targets_box, CB_RESETCONTENT, 0, 0);
        for (const auto& target : targets) {
            auto label = target.title + L"  [sekiro.exe, PID " + std::to_wstring(target.pid) + L"]";
            SendMessageW(targets_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (targets.empty()) SendMessageW(targets_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No visible sekiro.exe window found. Launch the game, then Refresh."));
        SendMessageW(targets_box, CB_SETCURSEL, 0, 0);
    }
    void sample_process() {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) { cpu_percent = unavailable; return; }
        auto ticks = [](FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        const auto total = ticks(kernel) + ticks(user);
        const auto now = qpc_ms();
        SYSTEM_INFO info{}; GetSystemInfo(&info);
        if (previous_wall > 0 && now > previous_wall && info.dwNumberOfProcessors)
            cpu_percent = (static_cast<double>(total - previous_cpu) / 10000.0) / (now - previous_wall) * 100.0 / info.dwNumberOfProcessors;
        previous_cpu = total; previous_wall = now;
    }
    void update() {
        const auto m = engine.snapshot();
        if (closing && !m.active) { DestroyWindow(window); return; }
        EnableWindow(start, !closing && !m.active && !targets.empty());
        EnableWindow(stop, m.active && !closing);
        EnableWindow(refresh, !m.active && !closing); EnableWindow(targets_box, !m.active && !closing);
        EnableWindow(record, !m.active && !closing); EnableWindow(export_button, !m.active && m.trace_rows > 0 && !closing);
        sample_process();
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        const bool has_memory = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE;
        auto percentiles = [](const Percentiles& p) { return number(p.p50) + L" / " + number(p.p95) + L" / " + number(p.p99); };
        std::wostringstream text;
        text << (closing ? L"Closing; waiting for capture shutdown..." : state_name(m.state)) << L"\r\n" << m.detail
             << L"\r\n\r\nAdapter: " << m.adapter << L"  | LUID " << m.adapter_luid_high << L":" << m.adapter_luid_low
             << L"\r\nContent: " << m.width << L" x " << m.height << L"  | Generation: " << m.generation << L"  | SDR BGRA8"
             << L"\r\nWGC received FPS: " << number(m.capture_fps) << L"  | GPU copy FPS: " << number(m.copy_fps)
             << L"\r\nFPS uses a 2-second window; distinct game-present FPS is unavailable."
             << L"\r\nSource intervals p50 / p95 / p99: " << percentiles(m.source_intervals) << L" ms"
             << L"\r\nDequeue age p50 / p95 / p99: " << percentiles(m.dequeue_age) << L" ms"
             << L"\r\nGPU-ready observed age p50 / p95 / p99: " << percentiles(m.ready_age) << L" ms"
             << L"\r\nGPU copy p50 / p95 / p99: " << percentiles(m.gpu_copy) << L" ms"
             << L"\r\nReady age includes completion polling delay; latency history: latest 2,048 samples."
             << L"\r\nReceived / copied: " << m.received << L" / " << m.completed
             << L"  | In flight: " << m.in_flight << L" / 3 (peak " << m.peak_in_flight << L")"
             << L"\r\nDropped: superseded " << m.dropped_newest << L", busy " << m.dropped_busy << L", resize " << m.dropped_resize << L", invalid " << m.dropped_invalid
             << L"\r\nCallback gaps >100 ms: " << m.callback_gaps << L"  | No new frame for: " << number(m.source_silence_ms) << L" ms"
             << L"\r\nInvalid/disjoint GPU times: " << m.invalid_gpu_times
             << L"\r\nProcess CPU (machine-normalized): " << number(cpu_percent) << L"%  | Working set: "
             << number(has_memory ? static_cast<double>(memory.WorkingSetSize) / 1048576.0 : unavailable) << L" MiB"
             << L"\r\nGPU utilization: N/A (external measurement required)  | Copied: " << number(static_cast<double>(m.copied_bytes) / 1048576.0) << L" MiB"
             << L"\r\nTrace: " << (m.recording ? L"enabled" : L"off") << L"  | Rows: " << m.trace_rows << L" / 120000"
             << (m.trace_full ? L"  | FULL: recording truncated" : L"")
             << L"\r\n\r\nM1 hardware acceptance: NOT MEASURED. Read docs/capture-runbook.md.";
        SetWindowTextW(stats, text.str().c_str());
    }
    void command(int id) {
        if (closing) return;
        if (id == id_refresh) refresh_windows();
        else if (id == id_stop) engine.request_stop();
        else if (id == id_start) {
            auto selected = SendMessageW(targets_box, CB_GETCURSEL, 0, 0);
            if (selected < 0 || static_cast<std::size_t>(selected) >= targets.size()) return;
            if (!target_is_current(targets[static_cast<std::size_t>(selected)])) {
                MessageBoxW(window, L"The selected game window changed. Refresh and select it again.", L"SekiroVisionAI", MB_OK | MB_ICONINFORMATION); return;
            }
            engine.start(targets[static_cast<std::size_t>(selected)], SendMessageW(record, BM_GETCHECK, 0, 0) == BST_CHECKED);
        } else if (id == id_export) {
            std::array<wchar_t, 32768> path{};
            wcscpy_s(path.data(), path.size(), L"sekiro-capture.csv");
            OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window;
            dialog.lpstrFilter = L"Capture trace (*.csv)\0*.csv\0"; dialog.lpstrFile = path.data();
            dialog.nMaxFile = static_cast<DWORD>(path.size()); dialog.lpstrDefExt = L"csv";
            dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&dialog)) {
                std::wstring error;
                const bool ok = engine.export_trace(path.data(), error);
                MessageBoxW(window, ok ? L"Saved CSV and its .meta.json sidecar." : error.c_str(), L"Export trace", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
            }
        }
        update();
    }
};

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<Probe*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Probe*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    try {
        if (app) switch (message) {
        case WM_CREATE: app->create(hwnd); return 0;
        case WM_SIZE: if (app->stats) app->layout(); return 0;
        case WM_COMMAND:
            // EDIT notifications during SetWindowText must not recursively refresh the UI.
            if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) >= id_refresh && LOWORD(wparam) <= id_export)
                app->command(LOWORD(wparam));
            return 0;
        case WM_TIMER: app->update(); return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam); info->ptMinTrackSize = {app->px(900), app->px(640)}; return 0;
        }
        case WM_DPICHANGED: {
            app->dpi = HIWORD(wparam); app->apply_font();
            const auto* rect = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            app->layout(); return 0;
        }
        case WM_CLOSE: app->closing = true; app->engine.request_stop(); app->update(); return 0;
        case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
        }
    } catch (...) {
        if (app) app->engine.request_stop();
        MessageBoxW(hwnd, L"The diagnostic UI encountered an error. Capture has been asked to stop.", L"SekiroVisionAI", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) return -1;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        Probe app;
        WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = window_proc;
        cls.lpszClassName = L"SekiroVisionAI.CaptureProbe"; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassExW(&cls)) return 1;
        auto hwnd = CreateWindowExW(0, cls.lpszClassName, L"SekiroVisionAI - Capture Probe", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1040, 730, nullptr, nullptr, instance, &app);
        if (!hwnd) return 1;
        ShowWindow(hwnd, show); UpdateWindow(hwnd);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        return static_cast<int>(message.wParam);
    } catch (...) {
        MessageBoxW(nullptr, L"Could not initialize the capture probe.", L"SekiroVisionAI", MB_OK | MB_ICONERROR); return 1;
    }
}
