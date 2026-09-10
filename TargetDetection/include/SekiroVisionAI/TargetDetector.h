#pragma once
#include <SekiroVisionAI/TargetTracker.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sekiro {
struct TargetModelStatus {
    bool loaded{}, trained{}, semantic_supported{}, production_validated{};
    int input_width{}, input_height{};
    double inference_ms{}, preprocessing_ms{};
    std::string version, provider, requested_provider, reason{"NO_TARGET_MODEL"};
};

// Single-owner native role detection. It identifies visible actor body boxes;
// attack/threat/timing remain exclusively temporal model/decision outputs.
class TargetDetector {
public:
    TargetDetector();
    ~TargetDetector();
    void load(const std::filesystem::path& path,const std::string& provider="AUTO");
    std::vector<TargetDetection> process(const SmallFrame& frame);
    TargetModelStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Full game RGB / 255, planar NCHW, half-pixel bilinear resize without cropping.
// Kept separately from UI/heuristic preview preprocessing and tested for parity.
std::vector<float> target_frame_rgb(const ColorFrame& frame,int width,int height);
}
