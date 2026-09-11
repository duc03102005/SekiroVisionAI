#pragma once
#include <SekiroVisionAI/ModelPrediction.h>
#include <SekiroVisionAI/MotionDetector.h>
#include <filesystem>
#include <memory>

namespace sekiro {
// Single-owner inference object: construct/load/process only on the vision worker.
class TemporalModel {
public:
    TemporalModel();
    ~TemporalModel();
    void load(const std::filesystem::path& path,const std::string& provider="CPU");
    void reset_history();
    ModelPrediction process(const SmallFrame& frame,const CombatRoi& roi);
    ModelStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// This preprocessing is also tested against the Python dataset resize contract.
std::vector<float> model_crop_rgb(const ColorFrame& frame,const CombatRoi& roi,int size);
}
