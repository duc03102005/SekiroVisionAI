#pragma once
#include <SekiroVisionAI/TargetTracker.h>
#include <SekiroVisionAI/TargetDetector.h>
#include <SekiroVisionAI/TemporalDecision.h>
#include <SekiroVisionAI/TemporalModel.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

namespace sekiro {
enum class DodgeDirection : int { Neutral=0, Left=1, Right=2, Back=3, Forward=4 };
inline constexpr const char* dodge_direction_name(DodgeDirection value) {
    switch(value) {
    case DodgeDirection::Left:return "A+Shift";
    case DodgeDirection::Right:return "D+Shift";
    case DodgeDirection::Back:return "S+Shift";
    case DodgeDirection::Forward:return "W+Shift";
    default:return "Shift";
    }
}
struct DirectionChoice {
    bool valid{}, geometry_verified{};
    DodgeDirection direction{DodgeDirection::Left};
    double confidence{};
    const char* reason{"DIRECTION_UNKNOWN"};
};
struct CombatPipelineConfig {
    CombatRoi roi; // Used only when automatic_roi is false (Debug).
    bool automatic_roi{true}, require_semantic_targets{true};
    bool allow_provisional_side_dodge{true};
    int detector_mode{1}; // 1 temporal model, 0 explicitly selected CV Debug.
    int direction_override{-1}; // Debug only: -1 selects the shared chooser.
    ThreatConfig heuristic;
    TemporalPolicy temporal;
    std::filesystem::path model_path;
    std::filesystem::path target_model_path; // Empty selects sibling targets.onnx.
    std::string provider{"DirectML"};
};
struct CombatContext {
    bool auto_enabled{}, foreground{}, operational{};
    std::uint64_t revision{};
};
struct CombatResult {
    TargetState targets;
    CombatRoi roi;
    MotionSignals motion;
    ModelPrediction prediction;
    ModelStatus model;
    TargetModelStatus target_model;
    TemporalAction action;
    DirectionChoice direction;
    bool new_prediction{}, request_dodge{};
    double decision_ms{}, processing_ms{};
    std::uint64_t revision{}, sequence{}, generation{};
};

DirectionChoice choose_dodge_direction(const TargetState&,const ModelPrediction&,
                                      bool allow_provisional_side_dodge,int debug_override=-1);

// The same object is linked by the live application and ReplayHarness. It owns
// target tracking, model preprocessing/history, threat tokens and direction policy.
// It never sends operating-system input. The live InputService rechecks the
// returned action at dispatch; replay labels its separate sink SIMULATED_INPUT.
class CombatPipeline {
public:
    using Clock=std::function<double()>;
    CombatPipeline();
    ~CombatPipeline();
    CombatPipeline(const CombatPipeline&)=delete;
    CombatPipeline& operator=(const CombatPipeline&)=delete;
    void configure(const CombatPipelineConfig&);
    void reset();
    void cancel_pending();
    ModelStatus model_status() const;
    TargetModelStatus target_model_status() const;
    CombatResult process(const SmallFrame&,const CombatContext&,const Clock&,
                         std::span<const TargetDetection> detections={});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
