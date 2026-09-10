#pragma once
#include <SekiroVisionAI/InputService.h>
#include <SekiroVisionAI/TemporalModel.h>
#include <SekiroVisionAI/TemporalDecision.h>
#include <SekiroVisionAI/SampleRecorder.h>
#include <SekiroVisionAI/CombatPipeline.h>

namespace sekiro {
struct MvpConfig {
    CombatRoi roi;
    ThreatConfig threat;
    int direction{-1}, hold_ms{45}, preset{1};
    bool automatic_roi{true};
    int detector_mode{1}; // 0 = explicit heuristic fallback, 1 = temporal model.
    TemporalPolicy temporal;
    std::filesystem::path model_path;
    std::string provider{"Auto"};
};
struct MvpSnapshot {
    SmallFrame preview;
    MotionSignals motion;
    ThreatDecision threat;
    InputStatus input;
    ModelPrediction prediction;
    ModelStatus model;
    RecordingStatus recording;
    CombatRoi combat_roi;
    std::string target_status{"Waiting for frames"}, direction_status{"No decision"};
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
    void toggle();
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
