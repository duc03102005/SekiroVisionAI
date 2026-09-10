#include <SekiroVisionAI/CombatPipeline.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace sekiro {
DirectionChoice choose_dodge_direction(const TargetState& targets,const ModelPrediction& prediction,
                                      bool allow_provisional,int debug_override) {
    DirectionChoice out;
    if(debug_override>=0&&debug_override<=4) {
        out.valid=true;out.direction=static_cast<DodgeDirection>(debug_override);
        out.reason="DEBUG_DIRECTION_OVERRIDE";return out;
    }
    if(prediction.valid&&(prediction.attack_class==4||prediction.attack_class==11)) {
        out.reason="NO_DODGE_GEOMETRY_FOR_SWEEP_OR_AOE";return out;
    }
    if(prediction.valid&&prediction.attack_direction_supported&&prediction.attack_direction==6&&
       prediction.attack_direction_confidence>=0.8) {
        out.reason="RADIAL_ATTACK_ESCAPE_UNSUPPORTED";return out;
    }
    const std::array<const EscapeCandidate*,4> escape{&targets.escape.left,&targets.escape.right,
        &targets.escape.backward,&targets.escape.forward};
    double best=-1;
    for(std::size_t i=0;i<escape.size();++i) {
        const auto& candidate=*escape[i];
        if(!candidate.valid||!std::isfinite(candidate.clearance)||!std::isfinite(candidate.confidence)||
           candidate.clearance<0.25||candidate.clearance>1||candidate.confidence<0.75||candidate.confidence>1)continue;
        const double score=candidate.clearance*candidate.confidence;
        if(score>best) {
            best=score;out.valid=true;out.geometry_verified=true;out.direction=static_cast<DodgeDirection>(i+1);
            out.confidence=candidate.confidence;out.reason="OBSERVED_ESCAPE_GEOMETRY";
        }
    }
    if(out.valid)return out;
    if(!allow_provisional){out.reason="NO_VERIFIED_ESCAPE_GEOMETRY";return out;}
    // Authorized first policy: lateral movement when a direction model cannot
    // establish safety. Image margin is not an arena boundary or free space.
    // A weapon's observed direction is never simply inverted into a safe key.
    double left_margin=0.5,right_margin=0.5;
    if(targets.wolf.valid&&targets.wolf.box.valid()) {
        left_margin=targets.wolf.box.left;right_margin=1-targets.wolf.box.right;
    }
    out.valid=true;out.direction=left_margin>=right_margin?DodgeDirection::Left:DodgeDirection::Right;
    out.confidence=0;out.reason="PROVISIONAL_SIDE_DODGE_SAFETY_UNKNOWN";
    return out;
}

struct CombatPipeline::Impl {
    CombatPipelineConfig config;
    TargetTracker targets;
    TargetDetector target_model;
    MotionDetector motion;
    TemporalModel model;
    TemporalDecision temporal;
    ThreatTracker heuristic;
    std::uint64_t revision{},generation{},sequence{},lineage{};
    double source_ms{};
    bool have_frame{},have_revision{};
    ModelPrediction last_prediction;
    void cancel() {temporal.cancel_candidate();heuristic.reset();}
    void reset() {
        targets.reset();motion.reset();model.reset_history();temporal.reset();heuristic.reset();
        generation=sequence=lineage=0;source_ms=0;have_frame=false;have_revision=false;last_prediction={};
    }
};
CombatPipeline::CombatPipeline():impl_(std::make_unique<Impl>()){}
CombatPipeline::~CombatPipeline()=default;
void CombatPipeline::configure(const CombatPipelineConfig& config) {
    if(!config.roi.valid()||config.detector_mode<0||config.detector_mode>1||config.direction_override < -1||config.direction_override>4)
        throw std::invalid_argument("Invalid shared combat pipeline configuration");
    impl_->config=config;impl_->reset();
    if(config.detector_mode==1) {
        impl_->model.load(config.model_path,config.provider);
        const auto target_path=config.target_model_path.empty()?config.model_path.parent_path()/"targets.onnx":config.target_model_path;
        impl_->target_model.load(target_path,config.provider);
    }
}
void CombatPipeline::reset(){impl_->reset();}
void CombatPipeline::cancel_pending(){impl_->cancel();}
ModelStatus CombatPipeline::model_status()const{return impl_->model.status();}
TargetModelStatus CombatPipeline::target_model_status()const{return impl_->target_model.status();}
CombatResult CombatPipeline::process(const SmallFrame& frame,const CombatContext& context,
                                     const Clock& clock,std::span<const TargetDetection> detections) {
    if(!clock)throw std::invalid_argument("CombatPipeline requires an aligned monotonic clock");
    auto& state=*impl_;const auto& config=state.config;CombatResult out;
    const double begin=clock();out.revision=context.revision;out.sequence=frame.sequence;out.generation=frame.generation;
    auto stop=[&](const char* reason) {
        state.cancel();out.action.decision.reason=reason;out.action.decision.state="OBSERVING";
        out.decision_ms=clock();out.processing_ms=std::max(0.0,out.decision_ms-begin);out.model=state.model.status();out.target_model=state.target_model.status();return out;
    };
    if(!std::isfinite(begin)||!std::isfinite(frame.source_ms)||frame.source_ms<=0||frame.sequence==0||frame.generation==0)
        return stop("INVALID_SOURCE_IDENTITY_OR_TIME");
    if(state.have_frame&&(frame.generation<state.generation||
        (frame.generation==state.generation&&(frame.sequence<=state.sequence||frame.source_ms<=state.source_ms))))
        return stop("OUT_OF_ORDER");
    if(state.have_frame&&frame.generation!=state.generation)state.reset();
    if(!state.have_revision||state.revision!=context.revision) {
        state.cancel();state.revision=context.revision;state.have_revision=true;
    }
    state.have_frame=true;state.sequence=frame.sequence;state.generation=frame.generation;state.source_ms=frame.source_ms;
    std::vector<TargetDetection> inferred_targets;
    if(detections.empty()&&config.detector_mode==1&&state.target_model.status().loaded) {
        inferred_targets=state.target_model.process(frame);detections=inferred_targets;
    }
    out.target_model=state.target_model.status();out.targets=state.targets.process(frame,detections);
    out.roi=config.automatic_roi?out.targets.roi:config.roi;
    if(!out.roi.valid())return stop("INVALID_COMBAT_ROI");
    if(state.lineage&&out.targets.track_lineage&&state.lineage!=out.targets.track_lineage) {
        state.model.reset_history();state.motion.reset();state.cancel();state.last_prediction={};
    }
    if(out.targets.track_lineage)state.lineage=out.targets.track_lineage;
    out.motion=state.motion.process(frame,out.roi);
    if(config.detector_mode==1) {
        out.prediction=state.model.process(frame,out.roi);out.new_prediction=out.prediction.valid;
        if(out.prediction.reason=="MODEL_SAMPLE_INTERVAL"&&state.last_prediction.valid)out.prediction=state.last_prediction;
        else state.last_prediction=out.prediction;
    } else {
        out.prediction.reason="EXPLICIT_CV_DEBUG_NO_TEMPORAL_MODEL";out.new_prediction=out.motion.valid;
    }
    out.model=state.model.status();out.decision_ms=clock();out.processing_ms=std::max(0.0,out.decision_ms-begin);
    if(out.new_prediction)out.prediction.prediction_ms=out.decision_ms;
    out.direction=choose_dodge_direction(out.targets,out.prediction,config.allow_provisional_side_dodge,config.direction_override);
    const char* blocked=nullptr;
    if(!context.auto_enabled)blocked="DISABLED";
    else if(!context.foreground)blocked="LOST_FOCUS";
    else if(!context.operational)blocked="CAPTURE_OR_WATCHDOG_UNAVAILABLE";
    else if(!std::isfinite(out.decision_ms)||out.decision_ms<frame.source_ms||
            out.decision_ms-frame.source_ms>(config.detector_mode?config.temporal.max_age_ms:config.heuristic.max_age_ms))blocked="STALE_FRAME";
    else if(config.require_semantic_targets&&(!out.targets.identity_certain||!out.targets.wolf.valid||!out.targets.enemy.valid))blocked="TARGET_IDENTITY_UNCONFIRMED";
    else if(!out.direction.valid)blocked=out.direction.reason;
    if(blocked) {state.cancel();out.action.decision.reason=blocked;out.action.decision.state="OBSERVING";return out;}
    if(config.detector_mode==1) {
        if(!out.new_prediction) {
            if(!out.prediction.valid)state.cancel();
            out.action.decision.reason=out.prediction.valid?"MODEL_SAMPLE_INTERVAL":"MODEL_NOT_READY";return out;
        }
        out.action=state.temporal.step(out.prediction,out.decision_ms,config.temporal);
    } else out.action.decision=state.heuristic.step(out.motion,out.decision_ms,config.heuristic);
    out.request_dodge=out.action.decision.trigger;
    return out;
}
}
