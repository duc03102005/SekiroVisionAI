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
// Observed attack/weapon trajectory, never an observed player dodge or safe key.
// Horizontal/vertical labels use image coordinates. TOWARD/AWAY use Wolf as the
// reference and require reviewed relative-geometry evidence in training.
inline constexpr const char* attack_directions[]{"LEFT_TO_RIGHT", "RIGHT_TO_LEFT",
    "TOP_TO_BOTTOM", "BOTTOM_TO_TOP", "TOWARD_WOLF", "AWAY_FROM_WOLF", "RADIAL", "UNKNOWN"};
struct ModelPrediction {
    bool valid{}, trained{}, attack_supported{}, threat_supported{}, tti_supported{}, auto_eligible{};
    bool attack_direction_supported{};
    std::uint64_t sequence{}, generation{};
    double source_ms{}, prediction_ms{}, inference_ms{};
    double attack_probability{}, threat_probability{}, tti_ms{}, tti_uncertainty_ms{};
    int state{}, attack_class{13}, observed_direction{4}, attack_direction{7};
    double attack_direction_confidence{}; // Uncalibrated softmax maximum, not safety probability.
    std::string version, provider, reason{"NO_MODEL"};
};
struct ModelStatus {
    bool loaded{}, trained{}, attack_supported{}, threat_supported{}, tti_supported{}, auto_eligible{};
    bool attack_direction_supported{}, provider_fallback{};
    int temporal_length{}, input_size{}, history_size{};
    double sample_interval_ms{};
    double load_ms{}, warmup_ms{};
    int warmup_runs{};
    std::string contract, requested_provider, provider_selection;
    std::string version, provider{"unavailable"}, reason{"NO_MODEL"};
};
}
