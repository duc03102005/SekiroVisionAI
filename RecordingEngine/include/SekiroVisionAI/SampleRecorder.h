#pragma once
#include <SekiroVisionAI/SmallFrame.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace sekiro {
struct RecordingStatus {
    bool enabled{};
    std::size_t queued{}; // Collecting, waiting and currently encoding combined.
    std::uint64_t saved{}, dropped{}, bytes_written{};
    std::string reason{"DISABLED"};
};

// Opt-in local recording, with no input injection or capture of its own.
// submit() retains a bounded number of immutable color-frame references only;
// JPEG encoding and all filesystem I/O happen on the recorder worker.
class SampleRecorder {
public:
    using Event = std::function<void(std::string event, std::string detail)>;
    explicit SampleRecorder(std::filesystem::path directory, Event event = {});
    ~SampleRecorder();
    SampleRecorder(const SampleRecorder&) = delete;
    SampleRecorder& operator=(const SampleRecorder&) = delete;

    void enabled(bool value);
    void submit(const SmallFrame& frame);
    // Call for an actual DODGE_SENT or a user review marker. A marker is never
    // an attack/TTI label. Its timestamp is the current QPC time.
    void mark(const std::string& reason, std::uint64_t episode = 0);
    // Flush partial samples and clear history on Stop/resize/focus loss.
    // The user's recording toggle is retained.
    void discontinuity(const std::string& reason = "CAPTURE_STOPPED");
    RecordingStatus status() const;
    std::filesystem::path path() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Windows CI check: actual WIC JPEG encode/decode, bounded marker admission,
// disabled-by-default behavior and truncated bundle manifest. No game/input.
bool sample_recorder_self_test(const std::filesystem::path& directory,
                               std::string& detail);
}
