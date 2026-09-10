#pragma once
#include <SekiroVisionAI/MotionDetector.h>
#include <cmath>
#include <cstdint>

namespace sekiro {
struct ThreatConfig {
    double enter_score{0.62}, exit_score{0.40}, min_confidence{0.55};
    double dwell_ms{40}, quiet_ms{220}, cooldown_ms{650}, warmup_ms{650}, max_age_ms{120};
};
struct ThreatDecision {
    bool trigger{};
    std::uint64_t episode{};
    const char* state{"OBSERVING"};
    const char* reason{"IDLE"};
};

// An episode is an observed motion burst. It is not a ground-truth attack identity.
class ThreatTracker {
public:
    void reset() { *this = {}; }
    ThreatDecision step(const MotionSignals& s, double now, const ThreatConfig& c) {
        ThreatDecision out; out.episode = episode_;
        if (!std::isfinite(s.source_ms) || !std::isfinite(now) || !std::isfinite(s.score) ||
            !std::isfinite(s.confidence) || s.source_ms <= 0 || s.source_ms > now || now-s.source_ms > c.max_age_ms) {
            candidate_since_ = quiet_since_ = 0; out.reason = "STALE_OR_INVALID"; return out;
        }
        if (last_ && s.source_ms <= last_) { candidate_since_ = quiet_since_ = 0; out.reason="OUT_OF_ORDER"; return out; }
        if (!first_) first_ = s.source_ms;
        const bool gap = last_ && s.source_ms-last_ > 120;
        last_ = s.source_ms;
        if (gap) { candidate_since_ = quiet_since_ = 0; neutral_ready_ = false; }
        if (!s.valid || s.camera_only) {
            candidate_since_ = quiet_since_ = 0;
            // Lost evidence cannot retire a consumed token or prove a new strike.
            out.state = consumed_ ? "CONSUMED" : "OBSERVING"; out.reason=s.reason; return out;
        }
        const bool quiet = s.score < c.exit_score && s.changed_fraction < 0.045;
        if (quiet) {
            candidate_since_ = 0;
            if (!quiet_since_) quiet_since_ = s.source_ms;
            if (s.source_ms - quiet_since_ >= c.quiet_ms) {
                neutral_ready_ = true;
                if (s.source_ms - last_action_ >= c.cooldown_ms) consumed_ = false;
            }
        } else quiet_since_ = 0;
        if (s.source_ms-first_ < c.warmup_ms) { out.state="WARMUP"; out.reason="TEMPORAL_WARMUP"; return out; }
        if (consumed_) { out.state="CONSUMED"; out.reason="ONE_DODGE_PER_EPISODE"; return out; }
        if (s.source_ms-last_action_ < c.cooldown_ms) { out.state="COOLDOWN"; out.reason="COOLDOWN"; return out; }
        if (!neutral_ready_) { out.reason="WAIT_FOR_QUIET"; return out; }
        if (quiet) return out;
        if (s.confidence < c.min_confidence || s.score < c.exit_score) {
            candidate_since_=0; out.reason="LOW_CONFIDENCE_OR_EXIT"; return out;
        }
        if (!candidate_since_) {
            const bool onset = s.acceleration > 0.025 || s.score >= c.enter_score+0.12;
            if (s.score < c.enter_score || !onset) { out.reason="BELOW_ENTRY_OR_NO_ONSET"; return out; }
            candidate_since_ = s.source_ms;
            ++episode_;
        }
        out.episode = episode_;
        out.state="CANDIDATE"; out.reason="ATTACK_ARMING";
        if (s.source_ms-candidate_since_ >= c.dwell_ms) {
            out.trigger=true; out.state="ARMED"; out.reason="THREAT_READY";
            consumed_=true; neutral_ready_=false; last_action_=s.source_ms; candidate_since_=0;
        }
        return out;
    }
private:
    double first_{}, last_{}, quiet_since_{}, candidate_since_{}, last_action_{-1e9};
    std::uint64_t episode_{};
    bool neutral_ready_{}, consumed_{};
};

struct DispatchContext {
    bool enabled{}, foreground{}, capture_running{}, watchdog_ready{}, key_conflict{};
    double source_ms{}, now_ms{}, cooldown_ms{650};
    std::uint64_t revision{}, expected_revision{}, episode{};
};
class DispatchGuard {
public:
    const char* reserve(const DispatchContext& c) {
        if (!c.enabled) return "DISABLED";
        if (!c.foreground) return "LOST_FOCUS";
        if (!c.capture_running || !c.watchdog_ready) return "CAPTURE_OR_WATCHDOG_UNAVAILABLE";
        if (c.revision != c.expected_revision) return "OLD_ARM_REVISION";
        if (!std::isfinite(c.source_ms) || !std::isfinite(c.now_ms) || c.source_ms<=0 || c.source_ms>c.now_ms || c.now_ms-c.source_ms>120) return "STALE_FRAME";
        if (c.episode==0 || (c.revision==last_revision_ && c.episode<=last_episode_)) return "CONSUMED_EPISODE";
        if (c.now_ms-last_ms_ < std::max(c.cooldown_ms,250.0)) return "COOLDOWN";
        if (c.key_conflict) return "PHYSICAL_KEY_CONFLICT";
        last_revision_=c.revision; last_episode_=c.episode; last_ms_=c.now_ms;
        return nullptr;
    }
private:
    std::uint64_t last_revision_{}, last_episode_{};
    double last_ms_{-1e9};
};
}
