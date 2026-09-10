#pragma once
#include <SekiroVisionAI/InputService.h>
#include <SekiroVisionAI/TemporalModel.h>
#include <SekiroVisionAI/TemporalDecision.h>
#include <SekiroVisionAI/SampleRecorder.h>

namespace sekiro {
struct MvpConfig {
    CombatRoi roi;
    ThreatConfig threat;
    int direction{}, hold_ms{45}, preset{1};
    int detector_mode{1}; // 0 = explicit heuristic fallback, 1 = temporal model.
    TemporalPolicy temporal;
    std::filesystem::path model_path;
    std::string provider{"DirectML"};
};
struct MvpSnapshot {
    SmallFrame preview;
    MotionSignals motion;
    ThreatDecision threat;
    InputStatus input;
    ModelPrediction prediction;
    ModelStatus model;
    RecordingStatus recording;
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
    void recording(bool enabled);
    void mark_sample(const std::string& reason);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
