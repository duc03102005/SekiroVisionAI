#pragma once
#include <SekiroVisionAI/EventLog.h>
#include <SekiroVisionAI/ThreatTracker.h>
#include <atomic>
#include <memory>
#include <optional>
#include <functional>

namespace sekiro {
struct InputStatus {
    bool enabled{}, arm_pending{}, hotkeys_ready{}, watchdog_ready{}, capture_running{}, detector_ready{};
    std::uint64_t revision{1}, sent{}, rejected{};
    std::string reason{"STARTING"};
};
struct DodgeRequest {
    std::uint64_t revision{}, episode{};
    double source_ms{};
    bool manual{};
    // Absolute QPC deadlines; zero means the explicit CV/manual fallback has no TTI estimate.
    double earliest_send_ms{}, latest_send_ms{};
    std::uint64_t sequence{};
    std::string model_version, prediction_details;
    // -1 uses the configured default; 0 Shift, 1 A, 2 D, 3 S, 4 W.
    int direction{-1};
};
#ifdef SVAI_INPUT_TESTING
// Only the dedicated Windows test target compiles this constructor/hooks.
// The application always validates sekiro.exe and uses the Windows backend.
struct InputTestHooks {
    std::function<bool(const WindowTarget&)> target_is_current;
    std::function<UINT(UINT,INPUT*,int)> send_input;
    std::function<void()> before_submit;
};
#endif
class InputService {
public:
    using EventCallback=std::function<void(const std::string&,std::uint64_t)>;
    explicit InputService(std::shared_ptr<EventLog> log,EventCallback callback={});
#ifdef SVAI_INPUT_TESTING
    InputService(std::shared_ptr<EventLog> log,EventCallback callback,InputTestHooks hooks);
#endif
    ~InputService();
    void target(const WindowTarget& target);
    void frame(double source_ms);
    // UI arming may wait up to 15 s for the selected game to gain foreground.
    // A second toggle or any disable/fault cancels that explicit pending arm.
    void toggle();
    void disable(const std::string& reason);
    void capture_stopped();
    void configure(int direction, int hold_ms, double cooldown_ms);
    void detector_ready(bool ready,const std::string& reason);
    void request(DodgeRequest request);
    InputStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Internal same-executable watchdog mode; returns empty for a normal application launch.
std::optional<int> input_watchdog_command_line();
}
