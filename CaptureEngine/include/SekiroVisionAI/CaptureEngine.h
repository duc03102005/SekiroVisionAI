#pragma once

#include "Timing.h"
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sekiro {
struct WindowTarget {
    HWND hwnd{};
    DWORD pid{};
    std::uint64_t process_creation_time{};
    std::wstring title;
};

std::vector<WindowTarget> find_sekiro_windows();
bool target_is_current(const WindowTarget& target);
double qpc_ms() noexcept;

enum class CaptureState { idle, starting, capturing, stopping, stopped, faulted };
const wchar_t* state_name(CaptureState state) noexcept;

struct CaptureSnapshot {
    CaptureState state{CaptureState::idle};
    std::wstring detail{L"Select a Sekiro window and press Start."};
    std::wstring adapter{L"unavailable"};
    std::uint32_t adapter_luid_low{};
    std::int32_t adapter_luid_high{};
    int width{}, height{};
    std::uint64_t generation{}, received{}, completed{}, dropped_newest{}, dropped_busy{}, dropped_resize{};
    std::uint64_t dropped_invalid{}, callback_notifications{}, callback_gaps{}, invalid_gpu_times{}, copied_bytes{};
    std::uint64_t trace_rows{};
    unsigned in_flight{}, peak_in_flight{};
    bool active{}, recording{}, trace_full{};
    double source_silence_ms{unavailable}, capture_fps{}, copy_fps{};
    Percentiles source_intervals, dequeue_age, ready_age, gpu_copy;
};

// UI/control calls are serialized by the host. snapshot() is safe during capture.
// This C++ API is internal; it is not the future C#/P/Invoke ABI.
class CaptureEngine {
public:
    CaptureEngine();
    ~CaptureEngine();
    CaptureEngine(const CaptureEngine&) = delete;
    CaptureEngine& operator=(const CaptureEngine&) = delete;
    bool start(const WindowTarget& target, bool record_trace);
    void request_stop() noexcept;
    [[nodiscard]] CaptureSnapshot snapshot() const;
    bool export_trace(const std::filesystem::path& path, std::wstring& error);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace sekiro
