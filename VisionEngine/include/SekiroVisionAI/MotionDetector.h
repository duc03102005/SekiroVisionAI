#pragma once
#include <SekiroVisionAI/SmallFrame.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sekiro {
struct CombatRoi {
    double left{0.28}, top{0.12}, right{0.72}, bottom{0.61};
    bool valid() const noexcept {
        return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) && std::isfinite(bottom) &&
            left >= 0.03 && top >= 0.03 && right <= 0.97 && bottom <= 0.92 && right - left >= 0.08 && bottom - top >= 0.08;
    }
};

struct MotionSignals {
    double source_ms{}, score{}, confidence{}, changed_fraction{}, excess{}, flow{}, acceleration{};
    double camera_dx{}, camera_dy{}, background_error{}, centroid_x{0.5}, centroid_y{0.4};
    bool valid{}, camera_only{};
    const char* reason{"WARMUP"};
};

// Dependency-free sparse block flow and compensated frame difference.
// Confidence is a heuristic quality score, never a calibrated attack probability.
class MotionDetector {
public:
    void reset() { previous_ = {}; have_previous_ = false; previous_energy_ = 0; }
    MotionSignals process(const SmallFrame& frame, const CombatRoi& roi) {
        MotionSignals s; s.source_ms = frame.source_ms;
        if (!roi.valid()) { s.reason = "INVALID_ROI"; return s; }
        if (!std::isfinite(frame.source_ms) || frame.source_ms <= 0) { s.reason = "INVALID_TIME"; return s; }
        const double dt = frame.source_ms - previous_.source_ms;
        if (!have_previous_ || frame.generation != previous_.generation || dt > 120) {
            previous_ = frame; have_previous_ = true; previous_energy_ = 0; return s;
        }
        if (dt <= 0) { s.reason = "OUT_OF_ORDER"; return s; }
        // Very short intervals do not provide stable integer block-flow evidence.
        if (dt < 7) { s.reason = "SAMPLE_INTERVAL"; return s; }
        const int l = static_cast<int>(roi.left * vision_width), r = static_cast<int>(roi.right * vision_width);
        const int t = static_cast<int>(roi.top * vision_height), b = static_cast<int>(roi.bottom * vision_height);
        auto pixel = [](const SmallFrame& f, int x, int y) { return static_cast<int>(f.gray[static_cast<std::size_t>(y * vision_width + x)]); };
        struct Point { int x, y; };
        std::vector<Point> background;
        background.reserve(1200);
        for (int y = 15; y < 104; y += 4) for (int x = 12; x < 244; x += 4) {
            if (x >= l - 8 && x <= r + 8 && y >= t - 8 && y <= b + 8) continue;
            if (y > 82 && x > 75 && x < 181) continue; // Wolf's usual screen region.
            background.push_back({x,y});
        }
        if (background.size() < 60) { previous_ = frame; s.reason = "ROI_TOO_LARGE"; return s; }
        double best = 1e9;
        int gx = 0, gy = 0;
        // Robust translation estimate excludes combat ROI, player and most HUD.
        for (int dy = -6; dy <= 6; ++dy) for (int dx = -6; dx <= 6; ++dx) {
            double error = 0;
            for (auto p : background) error += std::min(40, std::abs(pixel(frame,p.x,p.y) - pixel(previous_,p.x-dx,p.y-dy)));
            error /= static_cast<double>(background.size());
            const double penalized = error + 0.015 * (std::abs(dx) + std::abs(dy));
            if (penalized < best) { best = penalized; gx = dx; gy = dy; }
        }
        s.camera_dx = gx; s.camera_dy = gy;
        double bg_error = 0, bg_mean_delta = 0;
        for (auto p : background) {
            const int delta = pixel(frame,p.x,p.y) - pixel(previous_,p.x-gx,p.y-gy);
            bg_error += std::abs(delta); bg_mean_delta += delta;
        }
        bg_error /= static_cast<double>(background.size()); bg_mean_delta /= static_cast<double>(background.size());
        s.background_error = bg_error;
        double error = 0, mean = 0, square = 0, changed = 0, center_x = 0, center_y = 0;
        const double diff_threshold = std::max(12.0, bg_error * 1.7 + 5);
        int count = 0;
        for (int y = std::max(t, 10); y < std::min(b, vision_height-10); y += 2)
            for (int x = std::max(l, 10); x < std::min(r, vision_width-10); x += 2) {
                const double value = pixel(frame,x,y);
                const double difference = std::abs(value - pixel(previous_,x-gx,y-gy) - bg_mean_delta);
                mean += value; square += value * value; error += difference; ++count;
                if (difference > diff_threshold) { ++changed; center_x += x; center_y += y; }
            }
        if (!count) { previous_ = frame; s.reason = "INVALID_ROI"; return s; }
        mean /= count; error /= count;
        const double deviation = std::sqrt(std::max(0.0, square / count - mean * mean));
        s.changed_fraction = changed / count;
        if (changed > 0) { s.centroid_x = center_x / changed / vision_width; s.centroid_y = center_y / changed / vision_height; }
        s.excess = std::max(0.0, error - bg_error * 1.25 - 1);
        if (mean < 5 || deviation < 6) { previous_ = frame; previous_energy_ = 0; s.reason = "LOW_TEXTURE_OR_BLACK"; return s; }
        if (std::abs(gx) == 6 || std::abs(gy) == 6 || bg_error > 27 || std::abs(bg_mean_delta) > 18) {
            previous_ = frame; previous_energy_ = 0; s.camera_only = true; s.reason = "CAMERA_CUT_OR_FLASH"; return s;
        }
        double residual_flow = 0;
        int patches = 0, good = 0;
        for (int y = std::max(t+3,12); y < std::min(b-3,vision_height-12); y += 7)
            for (int x = std::max(l+3,12); x < std::min(r-3,vision_width-12); x += 7) {
                int low = 255, high = 0;
                for (int py=-2; py<=2; py+=2) for (int px=-2; px<=2; px+=2) {
                    const int v=pixel(frame,x+px,y+py); low=std::min(low,v); high=std::max(high,v);
                }
                if (high-low < 18) continue;
                ++patches;
                auto cost = [&](int dx,int dy) {
                    double sum = 0;
                    for (int py=-2; py<=2; py+=2) for (int px=-2; px<=2; px+=2)
                        sum += std::abs(pixel(frame,x+px,y+py)-pixel(previous_,x+px-gx-dx,y+py-gy-dy)-bg_mean_delta);
                    return sum / 9;
                };
                const double stationary = cost(0,0);
                double match = stationary;
                int fx = 0, fy = 0;
                for (int dy=-3; dy<=3; ++dy) for (int dx=-3; dx<=3; ++dx) {
                    double c = cost(dx,dy) + 0.1 * (std::abs(dx)+std::abs(dy));
                    if (c < match) { match=c; fx=dx; fy=dy; }
                }
                if (match < 28) {
                    ++good;
                    if (stationary - match > 2) residual_flow += std::hypot(fx,fy);
                }
            }
        s.flow = patches ? residual_flow / patches * (16.667 / dt) : 0;
        const double energy = s.excess / 12.0 + s.flow;
        s.acceleration = (energy - previous_energy_) * (16.667 / dt);
        previous_energy_ = 0.55 * energy + 0.45 * previous_energy_;
        const double localization = std::clamp((error-bg_error) / std::max(error,1.0),0.0,1.0);
        const double quality = patches ? static_cast<double>(good)/patches : 0;
        s.confidence = std::clamp(0.35*std::min(deviation/30,1.0) + 0.40*localization + 0.25*quality,0.0,1.0);
        s.score = std::clamp(0.35*std::min(s.excess/9,1.0) + 0.30*std::min(s.flow/0.8,1.0) +
            0.20*std::min(s.changed_fraction/0.14,1.0) + 0.15*std::clamp(s.acceleration/0.5,0.0,1.0),0.0,1.0);
        s.camera_only = error <= bg_error*1.6 + 2 && std::hypot(gx,gy) > 0;
        if (s.camera_only) s.score = 0;
        s.valid = true;
        s.reason = s.camera_only ? "CAMERA_ONLY" : s.changed_fraction < 0.012 ? "IDLE" : "LOCAL_MOTION";
        previous_ = frame;
        return s;
    }
private:
    SmallFrame previous_{};
    bool have_previous_{};
    double previous_energy_{};
};
}
