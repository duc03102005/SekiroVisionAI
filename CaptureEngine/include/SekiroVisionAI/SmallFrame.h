#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sekiro {
inline constexpr int vision_width = 256;
inline constexpr int vision_height = 144;
inline constexpr int color_readback_max_width = 1280;
inline constexpr int color_readback_max_height = 720;

// Immutable after publication through SmallFrame::color. Rows are top-down,
// tightly packed BGRA8 SDR; inference converts/crops these color pixels rather
// than using the legacy grayscale thumbnail. The full source texture remains
// on the capture GPU, while this bounded-size readback is a CPU copy.
struct ColorFrame {
    int width{}, height{}, stride{};
    std::vector<std::uint8_t> bgra;
};

struct SmallFrame {
    // Retained solely for the inexpensive heuristic detector and its tests.
    std::array<std::uint8_t, vision_width * vision_height> gray{};
    std::shared_ptr<const ColorFrame> color;
    int source_width{}, source_height{};
    std::uint64_t sequence{}, generation{};
    double source_ms{}, ready_ms{};
};
}
