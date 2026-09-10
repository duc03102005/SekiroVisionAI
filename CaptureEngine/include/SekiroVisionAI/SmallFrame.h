#pragma once
#include <array>
#include <cstdint>

namespace sekiro {
inline constexpr int vision_width = 256;
inline constexpr int vision_height = 144;
struct SmallFrame {
    std::array<std::uint8_t, vision_width * vision_height> gray{};
    std::uint64_t sequence{}, generation{};
    double source_ms{}, ready_ms{};
};
}
