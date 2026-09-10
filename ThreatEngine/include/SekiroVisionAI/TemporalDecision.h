#pragma once
#include <SekiroVisionAI/ModelPrediction.h>
#include <SekiroVisionAI/ThreatTracker.h>
#include <cmath>

namespace sekiro {
struct TemporalPolicy {
    double attack_threshold{0.8}, threat_threshold{0.8}, exit_threshold{0.35};
    double dwell_ms{33}, quiet_ms{180}, cooldown_ms{450};
    double lead_min_ms{70}, lead_max_ms{130}, max_uncertainty_ms{25}, max_age_ms{120};
};
struct TemporalAction {
    ThreatDecision decision;
    double remaining_ms{}, earliest_send_ms{}, latest_send_ms{};
};
// Only source-time observations advance dwell. A gap never retires a consumed strike.
class TemporalDecision {
public:
    void reset() { *this=TemporalDecision{}; }
    TemporalAction step(const ModelPrediction& p,double now,const TemporalPolicy& c) {
        TemporalAction out;
        out.decision.episode=episode_;
        auto no=[&](const char* state,const char* reason) {out.decision.state=state;out.decision.reason=reason;return out;};
        if(!p.valid || !std::isfinite(p.source_ms) || p.source_ms<=0 || !std::isfinite(now) ||
           !std::isfinite(p.attack_probability) || !std::isfinite(p.threat_probability) ||
           p.attack_probability<0 || p.attack_probability>1 || p.threat_probability<0 || p.threat_probability>1) {
            candidate_=-1;quiet_=-1;return no("observing","INVALID_OUTPUT");
        }
        if(p.source_ms<=last_source_)return no("observing","OUT_OF_ORDER");
        if(last_source_>0 && p.source_ms-last_source_>120) { candidate_=-1;quiet_=-1;ready_=false; }
        last_source_=p.source_ms;
        if(now<p.source_ms || now-p.source_ms>c.max_age_ms) {candidate_=-1;quiet_=-1;return no("observing","STALE_FRAME");}
        if(!p.trained) {candidate_=-1;return no("observing","MODEL_NOT_GAMEPLAY_TRAINED");}
        if(!p.attack_supported || !p.threat_supported || !p.tti_supported || !p.auto_eligible) {candidate_=-1;return no("observing","UNSUPPORTED_HEAD_OR_MODEL_NO_AUTO");}
        if(p.threat_probability<c.exit_threshold && p.attack_probability<c.exit_threshold) {
            candidate_=-1;
            if(quiet_<0)quiet_=p.source_ms;
            if(p.source_ms-quiet_>=c.quiet_ms && now-last_action_>=c.cooldown_ms) {consumed_=false;ready_=true;}
            return no(consumed_?"consumed":"observing",consumed_?"WAIT_NEW_STRIKE":"LOW_THREAT");
        }
        quiet_=-1;
        if(consumed_)return no("consumed","ONE_DODGE_PER_THREAT");
        if(!ready_)return no("observing","WAIT_QUIET_BEFORE_ARMING");
        if(p.attack_probability<c.attack_threshold || p.threat_probability<c.threat_threshold) {
            // Hysteresis retains a candidate above exit, but low-score observations never trigger.
            if(p.threat_probability<c.exit_threshold||p.attack_probability<c.exit_threshold)candidate_=-1;
            return no("candidate","LOW_CONFIDENCE_OR_THREAT");
        }
        if(candidate_<0){candidate_=p.source_ms;++episode_;out.decision.episode=episode_;}
        if(p.source_ms-candidate_<c.dwell_ms)return no("candidate","INSUFFICIENT_DWELL");
        if(!std::isfinite(p.tti_ms)||!std::isfinite(p.tti_uncertainty_ms)||p.tti_ms<0||p.tti_uncertainty_ms<0)
            return no("armed","UNKNOWN_TTI");
        out.remaining_ms=p.tti_ms-(now-p.source_ms);
        if(p.tti_uncertainty_ms>c.max_uncertainty_ms)return no("armed","TTI_TOO_WIDE");
        // Require the whole estimated contact interval inside the configurable lead window.
        out.earliest_send_ms=p.source_ms+p.tti_ms+p.tti_uncertainty_ms-c.lead_max_ms;
        out.latest_send_ms=p.source_ms+p.tti_ms-p.tti_uncertainty_ms-c.lead_min_ms;
        if(now>out.latest_send_ms)return no("armed","TOO_LATE");
        if(now<out.earliest_send_ms)return no("armed","WAIT_TTI_WINDOW");
        if(now-last_action_<c.cooldown_ms)return no("armed","COOLDOWN");
        // This first policy has no learned safe-direction evidence for sweeps/AOE.
        if(p.attack_class==4 || p.attack_class==11)return no("armed","UNSUPPORTED_DODGE_GEOMETRY");
        consumed_=true;ready_=false;last_action_=now;out.decision.trigger=true;
        out.decision.state="consumed";out.decision.reason="MODEL_THREAT_READY";return out;
    }
private:
    double last_source_{-1},candidate_{-1},quiet_{-1},last_action_{-1e9};
    bool consumed_{},ready_{};
    std::uint64_t episode_{};
};
}
