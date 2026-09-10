#pragma once

#include "SmallFrame.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sekiro {
inline bool valid_color_frame(const ColorFrame& color) noexcept {
    if (color.width <= 0 || color.height <= 0 || color.width > std::numeric_limits<int>::max() / 4 ||
        color.stride < color.width * 4) return false;
    const auto required = static_cast<std::uint64_t>(color.stride) * (color.height - 1) +
        static_cast<std::uint64_t>(color.width) * 4;
    return required <= color.bgra.size();
}

// Shared by live capture and native video replay. This is the heuristic/target
// motion derivative; the temporal model still receives the BGRA color frame.
// Does not modify frame source time, dimensions, sequence, or generation.
inline void derive_gray(const ColorFrame& color, SmallFrame& frame) {
    if (!valid_color_frame(color)) throw std::invalid_argument("Malformed BGRA color frame");
    const auto luminance = [&](int x, int y) {
        const auto* pixel = color.bgra.data() + static_cast<std::size_t>(y) * color.stride + static_cast<std::size_t>(x) * 4;
        return 0.114 * pixel[0] + 0.587 * pixel[1] + 0.299 * pixel[2];
    };
    // This derivative exists only for the heuristic detector. The temporal
    // model and UI consume ColorFrame; neither reconstructs color from gray.
    for (int y = 0; y < vision_height; ++y) {
        const double sy = std::clamp((y + 0.5) * color.height / vision_height - 0.5, 0.0, static_cast<double>(color.height - 1));
        const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, color.height - 1);
        const double fy = sy - y0;
        for (int x = 0; x < vision_width; ++x) {
            const double sx = std::clamp((x + 0.5) * color.width / vision_width - 0.5, 0.0, static_cast<double>(color.width - 1));
            const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, color.width - 1);
            const double fx = sx - x0;
            const double top = luminance(x0, y0) * (1.0 - fx) + luminance(x1, y0) * fx;
            const double bottom = luminance(x0, y1) * (1.0 - fx) + luminance(x1, y1) * fx;
            frame.gray[static_cast<std::size_t>(y * vision_width + x)] = static_cast<std::uint8_t>(std::lround(top * (1.0 - fy) + bottom * fy));
        }
    }
}
} // namespace sekiro
