#pragma once
#include <SekiroVisionAI/InputService.h>

namespace sekiro {
struct MvpConfig {
    CombatRoi roi;
    ThreatConfig threat;
    int direction{}, hold_ms{45}, preset{1};
};
struct MvpSnapshot {
    SmallFrame preview;
    MotionSignals motion;
    ThreatDecision threat;
    InputStatus input;
    bool has_frame{};
    std::uint64_t processed{}, replaced{}, threats{};
    double cpu_ms{}, frame_age_ms{};
};
class AutoDodge {
public:
    AutoDodge();
    ~AutoDodge();
    void start(const WindowTarget& target);
    void stop();
    void discontinuity();
    void submit(const SmallFrame& frame);
    void disable();
    MvpConfig config() const;
    void configure(MvpConfig config);
    MvpSnapshot snapshot() const;
    std::shared_ptr<EventLog> log() const;
    std::filesystem::path config_path() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
