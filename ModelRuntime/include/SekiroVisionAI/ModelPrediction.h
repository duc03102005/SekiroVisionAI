#pragma once
#include <cstdint>
#include <string>

namespace sekiro {
inline constexpr const char* attack_classes[]{
    "HORIZONTAL_SLASH", "VERTICAL_SLASH", "DIAGONAL_SLASH", "THRUST", "SWEEP", "GRAB",
    "JUMP_ATTACK", "OVERHEAD_ATTACK", "SPIN_ATTACK", "PROJECTILE", "CHARGE", "AOE",
    "MULTI_HIT_COMBO", "UNKNOWN_ATTACK"};
inline constexpr const char* movement_states[]{"IDLE", "WALK", "RUN", "TURN", "ATTACK_WINDUP",
    "ACTIVE_ATTACK", "RECOVERY", "COMBO_CONTINUATION", "FEINT"};
struct ModelPrediction {
    bool valid{}, trained{}, attack_supported{}, threat_supported{}, tti_supported{}, auto_eligible{};
    std::uint64_t sequence{}, generation{};
    double source_ms{}, prediction_ms{}, inference_ms{};
    double attack_probability{}, threat_probability{}, tti_ms{}, tti_uncertainty_ms{};
    int state{}, attack_class{13}, observed_direction{4};
    std::string version, provider, reason{"NO_MODEL"};
};
struct ModelStatus {
    bool loaded{}, trained{}, attack_supported{}, threat_supported{}, tti_supported{}, auto_eligible{};
    int temporal_length{}, input_size{}, history_size{};
    double sample_interval_ms{};
    std::string version, provider{"unavailable"}, reason{"NO_MODEL"};
};
}
