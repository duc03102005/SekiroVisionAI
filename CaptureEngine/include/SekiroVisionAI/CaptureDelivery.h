#pragma once

#include "Timing.h"
#include <cstdint>
#include <optional>
#include <span>

namespace sekiro {
// Same source-age budget as the live InputEngine. Completed pixels do not gain
// a new capture time when read back, dispatched, or drawn by the UI.
inline constexpr double live_capture_age_limit_ms = 120.0;

struct CaptureStamp {
    std::uint64_t sequence{}, generation{};
    double source_ms{};
};

struct CompletedCapture {
    CaptureStamp stamp;
    bool gpu_complete{};
};

// GPU completion can be observed in physical slot order after the ring wraps.
// Select one newest usable frame, and never publish an older completion after
// it. Source age and ordering are checked independently of callback/copy FPS.
class CaptureDelivery {
public:
    [[nodiscard]] bool newer(const CaptureStamp& stamp) const noexcept {
        return stamp.sequence > last_.sequence && stamp.generation > 0 && stamp.generation >= last_.generation &&
            std::isfinite(stamp.source_ms) && stamp.source_ms > last_.source_ms;
    }

    [[nodiscard]] std::optional<std::size_t> newest(
        std::span<const CompletedCapture> completed, double now_ms) const noexcept {
        std::optional<std::size_t> selected;
        for (std::size_t index = 0; index < completed.size(); ++index) {
            const auto& candidate = completed[index];
            if (!candidate.gpu_complete || !newer(candidate.stamp) ||
                !fresh_at(candidate.stamp.source_ms, now_ms, live_capture_age_limit_ms)) continue;
            if (!selected || candidate.stamp.sequence > completed[*selected].stamp.sequence)
                selected = index;
        }
        return selected;
    }

    // Call only once the sink has accepted this exact frame. The supplied time
    // is used for validation, never assigned to the source timestamp.
    bool delivered(const CaptureStamp& stamp, double observed_ms) noexcept {
        if (!newer(stamp) || !fresh_at(stamp.source_ms, observed_ms, live_capture_age_limit_ms)) return false;
        last_ = stamp;
        return true;
    }

private:
    CaptureStamp last_;
};
} // namespace sekiro
