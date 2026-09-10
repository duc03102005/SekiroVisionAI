#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace sekiro {
inline constexpr double unavailable = std::numeric_limits<double>::quiet_NaN();

// WGC SystemRelativeTime is already a QPC-derived TimeSpan, whose ticks are
// 100 ns. It is not a raw QueryPerformanceCounter value or a UTC timestamp.
inline double wgc_timespan_ms(std::int64_t ticks_100ns) noexcept {
    return ticks_100ns > 0 ? static_cast<double>(ticks_100ns) / 10000.0 : unavailable;
}

inline double qpc_ticks_ms(std::int64_t ticks, std::int64_t frequency) noexcept {
    if (ticks <= 0 || frequency <= 0) return unavailable;
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency);
}

inline double age_ms(double source, double observed) noexcept {
    if (!std::isfinite(source) || !std::isfinite(observed) || source <= 0 || observed < source)
        return unavailable;
    return observed - source;
}

inline bool fresh_at(double source, double observed, double maximum_age_ms) noexcept {
    const double age = age_ms(source, observed);
    return std::isfinite(maximum_age_ms) && maximum_age_ms > 0 &&
        std::isfinite(age) && age < maximum_age_ms;
}

struct Percentiles {
    double p50{unavailable}, p95{unavailable}, p99{unavailable}, maximum{unavailable};
};

// Bounded observation history. Invalid measurements never become a zero sample.
template<std::size_t Capacity = 2048>
class Series {
public:
    void add(double value) noexcept {
        if (!std::isfinite(value) || value < 0) return;
        values_[next_] = value;
        next_ = (next_ + 1) % Capacity;
        count_ = std::min(count_ + 1, Capacity);
    }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] Percentiles percentiles() const {
        if (!count_) return {};
        std::vector<double> sorted(values_.begin(), values_.begin() + count_);
        std::sort(sorted.begin(), sorted.end());
        auto percentile = [&](double p) { return sorted[static_cast<std::size_t>(std::ceil(p * count_)) - 1]; };
        return {percentile(0.50), percentile(0.95), percentile(0.99), sorted.back()};
    }
    // Fixed two-second denominator; startup ramps up and a stopped source decays to zero.
    [[nodiscard]] double rate(double now_ms, double window_ms = 2000.0) const noexcept {
        if (!std::isfinite(now_ms) || !std::isfinite(window_ms) || window_ms <= 0) return unavailable;
        std::size_t n = 0;
        for (std::size_t i = 0; i < count_; ++i)
            if (values_[i] > now_ms - window_ms && values_[i] <= now_ms) ++n;
        return static_cast<double>(n) * 1000.0 / window_ms;
    }
private:
    std::array<double, Capacity> values_{};
    std::size_t next_{}, count_{};
};
} // namespace sekiro
