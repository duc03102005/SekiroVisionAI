#pragma once
#include <SekiroVisionAI/SmallFrame.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sekiro::replay {
struct VideoFrame {
    std::shared_ptr<const ColorFrame> color;
    double pts_ms{}; // Preserved decoder presentation clock; not assumed zero-based.
    std::uint64_t sequence{}, generation{1};
};
class VideoReader {
public:
    virtual ~VideoReader()=default;
    virtual bool next(VideoFrame&)=0;
};
// Windows uses Media Foundation for MP4 and other installed native codecs.
// .svr is the portable lossless developer fixture format, never a game model.
std::unique_ptr<VideoReader> open_video(const std::filesystem::path&);
std::unique_ptr<VideoReader> open_raw_video(const std::filesystem::path&);
std::string sha256_file(const std::filesystem::path&);

struct AcceptedDodge {
    double timestamp_ms{};
    std::uint64_t sequence{}, episode{};
    int direction{};
    std::string attack_class;
};
struct Label {
    std::string id, kind, attack_class, evidence;
    double start_ms{}, end_ms{}, impact_min_ms{}, impact_max_ms{};
    double lead_min_ms{}, lead_max_ms{};
    bool reviewed{};
};
struct Outcome {
    std::string classification, event_id, reason;
    int action_index{-1};
    double timestamp_ms{}, earliest_ms{}, latest_ms{};
};
struct Acceptance {
    std::vector<Outcome> outcomes;
    std::uint64_t success{}, early{}, late{}, false_dodge{}, missed{}, unevaluated{}, excluded{};
    std::uint64_t false_in_negative{}, evaluable_events{}, accepted_actions{};
    double negative_minutes{};
};
std::vector<Label> read_labels(const std::filesystem::path&);
Acceptance evaluate(const std::vector<AcceptedDodge>&,const std::vector<Label>&);
void write_acceptance(const std::filesystem::path&,const Acceptance&);
std::string json_string(const std::string&);
}
