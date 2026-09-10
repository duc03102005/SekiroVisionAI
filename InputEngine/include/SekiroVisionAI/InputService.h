#pragma once
#include <SekiroVisionAI/EventLog.h>
#include <SekiroVisionAI/ThreatTracker.h>
#include <atomic>
#include <memory>
#include <optional>
#include <functional>

namespace sekiro {
struct InputStatus {
    bool enabled{}, hotkeys_ready{}, watchdog_ready{}, capture_running{}, detector_ready{true};
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
};
class InputService {
public:
    using EventCallback=std::function<void(const std::string&,std::uint64_t)>;
    explicit InputService(std::shared_ptr<EventLog> log,EventCallback callback={});
    ~InputService();
    void target(const WindowTarget& target);
    void frame(double source_ms);
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
