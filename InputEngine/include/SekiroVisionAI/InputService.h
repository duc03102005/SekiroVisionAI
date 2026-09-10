#pragma once
#include <SekiroVisionAI/EventLog.h>
#include <SekiroVisionAI/ThreatTracker.h>
#include <atomic>
#include <memory>
#include <optional>

namespace sekiro {
struct InputStatus {
    bool enabled{}, hotkeys_ready{}, watchdog_ready{}, capture_running{};
    std::uint64_t revision{1}, sent{}, rejected{};
    std::string reason{"STARTING"};
};
struct DodgeRequest {
    std::uint64_t revision{}, episode{};
    double source_ms{};
    bool manual{};
};
class InputService {
public:
    explicit InputService(std::shared_ptr<EventLog> log);
    ~InputService();
    void target(const WindowTarget& target);
    void frame(double source_ms);
    void disable(const std::string& reason);
    void capture_stopped();
    void configure(int direction, int hold_ms, double cooldown_ms);
    void request(DodgeRequest request);
    InputStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Internal same-executable watchdog mode; returns empty for a normal application launch.
std::optional<int> input_watchdog_command_line();
}
