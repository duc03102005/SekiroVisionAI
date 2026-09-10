#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sekiro {
inline constexpr int vision_width = 256;
inline constexpr int vision_height = 144;
// Compatibility limits for recording bundle input; these no longer cap AI.
inline constexpr int color_readback_max_width = 1280;
inline constexpr int color_readback_max_height = 720;
inline constexpr int preview_max_width = 640;
inline constexpr int preview_max_height = 360;

// Immutable after publication. Rows are top-down, tightly packed BGRA8 SDR.
// Capture publishes source-resolution pixels in color for AI and an independent
// GPU-scaled debug image in preview_color. Native source size is capped at
// 8.3 megapixels/8192 per dimension by CaptureEngine. This is an explicit CPU
// readback bridge; no GPU tensor or zero-copy inference is implied.
struct ColorFrame {
    int width{}, height{}, stride{};
    std::vector<std::uint8_t> bgra;
};

struct SmallFrame {
    // Retained solely for the inexpensive heuristic detector and its tests.
    std::array<std::uint8_t, vision_width * vision_height> gray{};
    std::shared_ptr<const ColorFrame> color;
    std::shared_ptr<const ColorFrame> preview_color;
    int source_width{}, source_height{};
    std::uint64_t sequence{}, generation{};
    double source_ms{}, ready_ms{};
};
}
