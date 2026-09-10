#include <SekiroVisionAI/SampleRecorder.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#ifndef SVAI_BUILD_REVISION
#define SVAI_BUILD_REVISION "unknown"
#endif

namespace sekiro {
namespace {
using Microsoft::WRL::ComPtr;
constexpr double sample_interval_ms = 1000.0 / 15.0;
constexpr double pre_roll_ms = 1000.0, post_roll_ms = 1000.0;
constexpr double maximum_gap_ms = 250.0;
constexpr std::size_t maximum_history_frames = 17, maximum_clip_frames = 33;
constexpr std::size_t maximum_jobs = 2;
constexpr std::uint64_t session_budget = 2ull * 1024 * 1024 * 1024;
constexpr std::uint64_t metadata_reserve = 64ull * 1024;
constexpr std::size_t maximum_encoded_frame = 8u * 1024 * 1024;

double recorder_qpc_ms() {
    static const double frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart) * 1000.0 / frequency;
}

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4)
                             << std::setfill('0') << static_cast<int>(c) << std::dec;
        else out << c;
    }
    out << '"';
    return out.str();
}

std::string marker_reason(const std::string& text) {
    // Only a short review category is accepted, never a filename or JSON.
    std::string result;
    result.reserve(64);
    for (const unsigned char c : text) {
        if (result.size() == 64) break;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') result += static_cast<char>(c);
    }
    return result.empty() ? "MANUAL_REVIEW" : result;
}

std::string session_name() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::ostringstream name;
    name << "session-" << std::setfill('0') << std::setw(4) << time.wYear
         << std::setw(2) << time.wMonth << std::setw(2) << time.wDay << '-'
         << std::setw(2) << time.wHour << std::setw(2) << time.wMinute
         << std::setw(2) << time.wSecond << '-' << GetCurrentProcessId()
         << '-' << GetTickCount64();
    return name.str();
}

std::string numbered_name(const char* prefix, std::uint64_t number, const char* suffix = "") {
    std::ostringstream out;
    out << prefix << std::setfill('0') << std::setw(6) << number << suffix;
    return out.str();
}

void checked(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream message;
        message << operation << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
        throw std::runtime_error(message.str());
    }
}

struct Apartment {
    Apartment() { checked(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx"); }
    ~Apartment() { CoUninitialize(); }
};

struct Frame {
    std::shared_ptr<const ColorFrame> color;
    std::uint64_t sequence{}, generation{};
    int source_width{}, source_height{};
    double source_ms{}, ready_ms{};
};

Frame retain(const SmallFrame& frame) {
    return {frame.color, frame.sequence, frame.generation, frame.source_width,
            frame.source_height, frame.source_ms, frame.ready_ms};
}

bool valid_frame(const SmallFrame& frame) {
    if (!frame.color || !std::isfinite(frame.source_ms) || frame.source_ms <= 0 ||
        !std::isfinite(frame.ready_ms) || frame.ready_ms < frame.source_ms ||
        frame.source_width <= 0 || frame.source_height <= 0) return false;
    const auto& color = *frame.color;
    if (color.width <= 0 || color.height <= 0 ||
        color.width > color_readback_max_width || color.height > color_readback_max_height ||
        color.stride != color.width * 4) return false;
    const auto bytes = static_cast<std::size_t>(color.stride) * static_cast<std::size_t>(color.height);
    return color.bgra.size() == bytes;
}

struct Job {
    std::uint64_t id{}, episode{};
    std::string reason, completion;
    double marker_ms{};
    bool sealed{};
    std::vector<Frame> frames;
};

// Encode in memory before writing so the on-disk session budget can be checked
// before each file. The only BGRA-to-BGR copy runs here, never in submit().
std::vector<std::uint8_t> jpeg(IWICImagingFactory* factory, const ColorFrame& color) {
    const auto width = static_cast<UINT>(color.width);
    const auto height = static_cast<UINT>(color.height);
    const UINT stride = width * 3;
    std::vector<std::uint8_t> bgr(static_cast<std::size_t>(stride) * height);
    for (UINT y = 0; y < height; ++y) {
        const auto* source = color.bgra.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(color.stride);
        auto* destination = bgr.data() + static_cast<std::size_t>(y) * stride;
        for (UINT x = 0; x < width; ++x) {
            destination[x * 3] = source[x * 4];
            destination[x * 3 + 1] = source[x * 4 + 1];
            destination[x * 3 + 2] = source[x * 4 + 2];
        }
    }
    ComPtr<IStream> stream;
    checked(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Create JPEG memory stream");
    ComPtr<IWICBitmapEncoder> encoder;
    checked(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder), "Create JPEG encoder");
    checked(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize JPEG encoder");
    ComPtr<IWICBitmapFrameEncode> output;
    ComPtr<IPropertyBag2> options;
    checked(encoder->CreateNewFrame(&output, &options), "Create JPEG frame");
    PROPBAG2 quality{};
    wchar_t quality_name[] = L"ImageQuality";
    quality.pstrName = quality_name;
    VARIANT value{};
    value.vt = VT_R4;
    value.fltVal = 0.90f;
    checked(options->Write(1, &quality, &value), "Set JPEG quality");
    checked(output->Initialize(options.Get()), "Initialize JPEG frame");
    checked(output->SetSize(width, height), "Set JPEG dimensions");
    auto format = GUID_WICPixelFormat24bppBGR;
    checked(output->SetPixelFormat(&format), "Set JPEG pixel format");
    if (!IsEqualGUID(format, GUID_WICPixelFormat24bppBGR))
        throw std::runtime_error("JPEG encoder did not accept BGR24");
    checked(output->WritePixels(height, stride, static_cast<UINT>(bgr.size()), bgr.data()), "Encode JPEG pixels");
    checked(output->Commit(), "Commit JPEG frame");
    checked(encoder->Commit(), "Commit JPEG encoder");
    STATSTG information{};
    checked(stream->Stat(&information, STATFLAG_NONAME), "Read JPEG size");
    if (information.cbSize.QuadPart == 0 || information.cbSize.QuadPart > maximum_encoded_frame)
        throw std::runtime_error("Encoded JPEG exceeds per-frame memory bound");
    std::vector<std::uint8_t> encoded(static_cast<std::size_t>(information.cbSize.QuadPart));
    LARGE_INTEGER start{};
    checked(stream->Seek(start, STREAM_SEEK_SET, nullptr), "Rewind JPEG stream");
    ULONG read{};
    checked(stream->Read(encoded.data(), static_cast<ULONG>(encoded.size()), &read), "Read JPEG bytes");
    if (read != encoded.size()) throw std::runtime_error("Incomplete JPEG stream");
    return encoded;
}

void write_file(const std::filesystem::path& path, const char* data, std::size_t bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream.write(data, static_cast<std::streamsize>(bytes));
    stream.close();
}

std::string manifest(const Job& job, const std::string& session, std::size_t written) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6);
    const auto& first = job.frames.front();
    const auto& last = job.frames.at(written - 1);
    const bool before_complete = first.source_ms <= job.marker_ms - pre_roll_ms + sample_interval_ms * 1.5;
    const bool after_complete = last.source_ms >= job.marker_ms + post_roll_ms - sample_interval_ms * 1.5;
    out << "{\n  \"schema_version\":1,\n  \"format\":\"sekiro-jpeg-frame-bundle\",\n"
        << "  \"session_id\":" << quote(session) << ",\n  \"sample_id\":" << quote(numbered_name("sample-", job.id))
        << ",\n  \"git_commit\":" << quote(SVAI_BUILD_REVISION)
        << ",\n  \"marker\":{\"reason\":" << quote(job.reason) << ",\"episode\":" << job.episode
        << ",\"qpc_ms\":" << job.marker_ms << "},\n"
        << "  \"source\":{\"kind\":\"app_capture\",\"target\":\"sekiro.exe\",\"generation\":" << first.generation
        << ",\"width\":" << first.source_width << ",\"height\":" << first.source_height << "},\n"
        << "  \"annotation\":{\"status\":\"UNREVIEWED\",\"boss\":null,\"phase\":null,\"attack_class\":null,"
        << "\"threat\":null,\"impact_frame\":null,\"tti_ms\":null,\"dodge_direction\":null},\n"
        << "  \"timing\":{\"clock\":\"WGC SystemRelativeTime / QPC milliseconds\",\"target_sample_fps\":15,"
        << "\"requested_pre_ms\":1000,\"requested_post_ms\":1000,\"available_pre_ms\":"
        << std::max(0.0, job.marker_ms - first.source_ms) << ",\"available_post_ms\":"
        << std::max(0.0, last.source_ms - job.marker_ms)
        << ",\"pre_complete\":" << (before_complete ? "true" : "false")
        << ",\"post_complete\":" << (after_complete ? "true" : "false")
        << ",\"completion\":" << quote(job.completion) << "},\n"
        << "  \"image\":{\"codec\":\"Windows WIC JPEG\",\"quality\":0.9,\"lossy\":true,\"width\":"
        << first.color->width << ",\"height\":" << first.color->height << "},\n  \"frames\":[\n";
    for (std::size_t i = 0; i < written; ++i) {
        const auto& frame = job.frames[i];
        out << "    {\"index\":" << i << ",\"file\":" << quote(numbered_name("frame-", i, ".jpg"))
            << ",\"sequence\":" << frame.sequence << ",\"generation\":" << frame.generation
            << ",\"source_qpc_ms\":" << frame.source_ms << ",\"ready_qpc_ms\":" << frame.ready_ms
            << ",\"relative_to_marker_ms\":" << frame.source_ms - job.marker_ms << "}"
            << (i + 1 < written ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    return out.str();
}

std::string concat_manifest(const Job& job, std::size_t written) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "ffconcat version 1.0\n" << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < written; ++i) {
        out << "file '" << numbered_name("frame-", i, ".jpg") << "'\noption framerate 1000\n";
        if (i + 1 < written)
            out << "duration " << (job.frames[i + 1].source_ms - job.frames[i].source_ms) / 1000.0 << '\n';
    }
    // No repeated last frame: future content/duration is unknown. The JSON
    // source timestamps, not MP4/image-demuxer time-base rounding, are canonical.
    return out.str();
}
}

struct SampleRecorder::Impl {
    std::filesystem::path directory;
    std::string session;
    Event event;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<Frame> history;
    std::deque<Job> jobs;
    RecordingStatus state;
    bool quitting{}, encoding{}, failed{};
    std::uint64_t next_id{}, written_bytes{};
    std::thread worker;

    explicit Impl(std::filesystem::path base, Event callback)
        : session(session_name()), event(std::move(callback)) {
        directory = std::move(base) / session;
        worker = std::thread([this] { run(); });
    }
    ~Impl() {
        {
            std::lock_guard lock(mutex);
            quitting = true;
            state.enabled = false;
            seal("APP_EXIT");
            history.clear();
        }
        changed.notify_one();
        if (worker.joinable()) worker.join();
    }
    void emit(const std::string& name, const std::string& detail) const noexcept {
        try { if (event) event(name, detail); } catch (...) { /* Logging cannot stop capture. */ }
    }
    void seal(const std::string& reason) {
        for (auto& job : jobs) {
            if (!job.sealed) { job.sealed = true; job.completion = reason; }
        }
    }
    void disable_for_budget() {
        std::lock_guard lock(mutex);
        state.enabled = false;
        state.reason = "SESSION_DISK_BUDGET";
        state.dropped += jobs.size();
        jobs.clear();
        history.clear();
        failed = true; // A new session requires app restart, not toggle cycling.
    }
    bool save(IWICImagingFactory* factory, Job& job) {
        if (job.frames.empty()) throw std::runtime_error("Empty recording sample");
        if (written_bytes + metadata_reserve > session_budget) {
            disable_for_budget();
            return false;
        }
        const auto target = directory / numbered_name("sample-", job.id);
        std::filesystem::create_directories(directory);
        // Never overwrite an existing recording folder, even after a collision.
        if (!std::filesystem::create_directory(target)) throw std::runtime_error("Sample directory already exists");
        std::size_t count = 0;
        bool budget_reached = false;
        try {
            for (const auto& frame : job.frames) {
                const auto encoded = jpeg(factory, *frame.color);
                if (written_bytes + encoded.size() + metadata_reserve > session_budget) {
                    budget_reached = true;
                    job.completion = "SESSION_DISK_BUDGET";
                    break;
                }
                // Reserve before attempting I/O: even a partially failed write
                // counts against the session bound if cleanup also fails.
                written_bytes += encoded.size();
                write_file(target / numbered_name("frame-", count, ".jpg"),
                           reinterpret_cast<const char*>(encoded.data()), encoded.size());
                ++count;
            }
            if (count == 0) {
                std::error_code ignored;
                std::filesystem::remove_all(target, ignored);
                if (budget_reached) disable_for_budget();
                return false;
            }
            const auto json = manifest(job, session, count);
            const auto concat = concat_manifest(job, count);
            if (json.size() + concat.size() > metadata_reserve)
                throw std::runtime_error("Sample metadata exceeds reserved bound");
            written_bytes += json.size() + concat.size();
            write_file(target / "frames.ffconcat", concat.data(), concat.size());
            write_file(target / "sample.pending.json", json.data(), json.size());
            // A final manifest appears only after all admitted frames and the
            // concat file are closed. Incomplete folders have no sample.json.
            std::filesystem::rename(target / "sample.pending.json", target / "sample.json");
            if (budget_reached) disable_for_budget();
            return true;
        } catch (...) {
            // Only this newly-created sample folder can be removed. Already
            // written bytes still count against the conservative session budget.
            std::error_code ignored;
            std::filesystem::remove_all(target, ignored);
            throw;
        }
    }
    void run() noexcept {
        try {
            Apartment apartment;
            ComPtr<IWICImagingFactory> factory;
            checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&factory)), "Create WIC factory");
            for (;;) {
                std::optional<Job> selected;
                {
                    std::unique_lock lock(mutex);
                    changed.wait_for(lock, std::chrono::milliseconds(100), [&] {
                        return quitting || std::any_of(jobs.begin(), jobs.end(), [](const Job& item) { return item.sealed; });
                    });
                    const double now = recorder_qpc_ms();
                    for (auto& job : jobs) {
                        if (!job.sealed && now > job.marker_ms + post_roll_ms + maximum_gap_ms) {
                            job.sealed = true;
                            job.completion = "POST_ROLL_TIMEOUT";
                        }
                    }
                    auto ready = std::find_if(jobs.begin(), jobs.end(), [](const Job& item) { return item.sealed; });
                    if (ready != jobs.end()) {
                        selected = std::move(*ready);
                        jobs.erase(ready);
                        encoding = true;
                    } else if (quitting) break;
                }
                if (!selected) continue;
                bool saved = false;
                std::string detail;
                try {
                    saved = save(factory.Get(), *selected);
                    detail = numbered_name("sample-", selected->id) + " episode=" + std::to_string(selected->episode)
                           + " reason=" + selected->reason + " completion=" + selected->completion;
                } catch (const std::exception& error) { detail = error.what(); }
                // Release raw frame references before opening an admission
                // slot; otherwise a caller could briefly create a third job.
                selected.reset();
                {
                    std::lock_guard lock(mutex);
                    encoding = false;
                    state.bytes_written = written_bytes;
                    if (saved) ++state.saved; else ++state.dropped;
                    if (!failed) state.reason = saved ? "SAMPLE_SAVED" : "SAMPLE_WRITE_FAILED";
                }
                emit(saved ? "SAMPLE_SAVED" : "SAMPLE_DROPPED", detail);
            }
        } catch (const std::exception& error) {
            {
                std::lock_guard lock(mutex);
                failed = true;
                state.enabled = false;
                state.reason = "RECORDER_FAULT";
                state.dropped += jobs.size() + (encoding ? 1u : 0u);
                jobs.clear();
                history.clear();
                encoding = false;
            }
            emit("RECORDING_FAULT", error.what());
        } catch (...) {
            std::lock_guard lock(mutex);
            failed = true;
            state.enabled = false;
            state.reason = "RECORDER_FAULT";
            state.dropped += jobs.size() + (encoding ? 1u : 0u);
            jobs.clear();
            history.clear();
            encoding = false;
        }
    }
};

SampleRecorder::SampleRecorder(std::filesystem::path directory, Event event)
    : impl_(std::make_unique<Impl>(std::move(directory), std::move(event))) {}
SampleRecorder::~SampleRecorder() = default;

void SampleRecorder::enabled(bool value) {
    bool actual = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->quitting || impl_->failed) return;
        if (impl_->state.enabled == value) return;
        impl_->state.enabled = value;
        impl_->state.reason = value ? "WAITING_FOR_COLOR_FRAMES" : "DISABLED";
        if (!value) { impl_->seal("RECORDING_DISABLED"); impl_->history.clear(); }
        actual = impl_->state.enabled;
    }
    impl_->changed.notify_one();
    impl_->emit(actual ? "RECORDING_ENABLED" : "RECORDING_DISABLED",
                actual ? "Local JPEG frame bundles; labels remain UNREVIEWED" : "Partial samples are flushed without future padding");
}

void SampleRecorder::discontinuity(const std::string& reason) {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->seal(marker_reason(reason));
        impl_->history.clear();
        if (impl_->state.enabled) impl_->state.reason = "WAITING_FOR_COLOR_FRAMES";
    }
    impl_->changed.notify_one();
}

void SampleRecorder::submit(const SmallFrame& frame) {
    if (!valid_frame(frame)) return;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->state.enabled || impl_->quitting) return;
        if (!impl_->history.empty()) {
            const auto& previous = impl_->history.back();
            if (previous.generation != frame.generation || frame.sequence <= previous.sequence ||
                frame.source_ms <= previous.source_ms || frame.source_ms - previous.source_ms > maximum_gap_ms ||
                previous.color->width != frame.color->width || previous.color->height != frame.color->height ||
                previous.source_width != frame.source_width || previous.source_height != frame.source_height) {
                impl_->seal("CAPTURE_DISCONTINUITY");
                impl_->history.clear();
            } else if (frame.source_ms - previous.source_ms < sample_interval_ms - 0.5) return;
        }
        const Frame retained = retain(frame);
        impl_->history.push_back(retained);
        while (impl_->history.size() > maximum_history_frames ||
               (impl_->history.size() > 1 && impl_->history.front().source_ms < frame.source_ms - pre_roll_ms))
            impl_->history.pop_front();
        for (auto& job : impl_->jobs) {
            if (job.sealed) continue;
            if (job.frames.size() < maximum_clip_frames &&
                frame.source_ms > job.frames.back().source_ms &&
                frame.source_ms <= job.marker_ms + post_roll_ms + sample_interval_ms)
                job.frames.push_back(retained);
            if (frame.source_ms >= job.marker_ms + post_roll_ms || job.frames.size() >= maximum_clip_frames) {
                job.sealed = true;
                job.completion = job.frames.back().source_ms >= job.marker_ms + post_roll_ms - sample_interval_ms * 1.5
                    ? "WINDOW_COLLECTED" : "POST_ROLL_GAP";
            }
        }
        impl_->state.reason = "RECORDING_READY";
    }
    impl_->changed.notify_one();
}

void SampleRecorder::mark(const std::string& reason, std::uint64_t episode) {
    std::string outcome;
    bool accepted = false;
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->state.enabled || impl_->quitting) return;
        const double now = recorder_qpc_ms();
        if (impl_->jobs.size() + (impl_->encoding ? 1u : 0u) >= maximum_jobs) outcome = "RECORDING_QUEUE_FULL";
        else if (impl_->history.empty() || now - impl_->history.back().source_ms > maximum_gap_ms ||
                 now < impl_->history.back().source_ms - 5.0) outcome = "NO_FRESH_COLOR_HISTORY";
        else {
            Job job;
            job.id = ++impl_->next_id;
            job.episode = episode;
            job.reason = marker_reason(reason);
            job.marker_ms = now;
            job.frames.reserve(maximum_clip_frames);
            for (const auto& frame : impl_->history) {
                if (frame.source_ms >= now - pre_roll_ms - sample_interval_ms && frame.source_ms <= now)
                    job.frames.push_back(frame);
            }
            if (!job.frames.empty()) {
                outcome = numbered_name("sample-", job.id) + " episode=" + std::to_string(episode) + " reason=" + job.reason;
                impl_->jobs.push_back(std::move(job));
                accepted = true;
            } else outcome = "NO_FRESH_COLOR_HISTORY";
        }
        if (!accepted) { ++impl_->state.dropped; impl_->state.reason = outcome; }
    }
    impl_->changed.notify_one();
    impl_->emit(accepted ? "SAMPLE_MARKED" : "SAMPLE_DROPPED", outcome);
}

RecordingStatus SampleRecorder::status() const {
    std::lock_guard lock(impl_->mutex);
    auto result = impl_->state;
    result.queued = impl_->jobs.size() + (impl_->encoding ? 1u : 0u);
    return result;
}
std::filesystem::path SampleRecorder::path() const { return impl_->directory; }

bool sample_recorder_self_test(const std::filesystem::path& directory, std::string& detail) {
    try {
        std::filesystem::path recorded;
        {
            SampleRecorder recorder(directory);
            recorded = recorder.path();
            if (recorder.status().enabled) throw std::runtime_error("Recorder did not start disabled");
            auto color = std::make_shared<ColorFrame>();
            color->width = 64; color->height = 36; color->stride = 64 * 4;
            color->bgra.resize(static_cast<std::size_t>(color->stride) * 36);
            for (std::size_t i = 0; i < color->bgra.size(); i += 4) {
                color->bgra[i] = 12; color->bgra[i + 1] = 90;
                color->bgra[i + 2] = 220; color->bgra[i + 3] = 255;
            }
            SmallFrame frame;
            frame.color = color; frame.generation = 1;
            frame.source_width = 128; frame.source_height = 72;
            const double start = recorder_qpc_ms() - 1000.0;
            recorder.enabled(true);
            for (int i = 0; i < 15; ++i) {
                frame.sequence = static_cast<std::uint64_t>(i + 1);
                frame.source_ms = start + static_cast<double>(i) * sample_interval_ms;
                frame.ready_ms = frame.source_ms + 2.0;
                recorder.submit(frame);
            }
            recorder.mark("SELF_TEST", 42);
            recorder.mark("SELF_TEST_SECOND", 43);
            recorder.mark("QUEUE_BOUND", 44);
            const auto queued = recorder.status();
            if (queued.queued != 2 || queued.dropped != 1)
                throw std::runtime_error("Recording marker queue bound failed");
            recorder.enabled(false);
            // Destruction drains the two bounded, already truncated samples.
        }
        const auto first = recorded / "sample-000001";
        const auto second = recorded / "sample-000002";
        std::ifstream json_stream(first / "sample.json", std::ios::binary);
        const std::string json((std::istreambuf_iterator<char>(json_stream)), {});
        if (json.find("\"status\":\"UNREVIEWED\"") == std::string::npos ||
            json.find("\"post_complete\":false") == std::string::npos ||
            json.find("\"completion\":\"RECORDING_DISABLED\"") == std::string::npos ||
            !std::filesystem::exists(second / "sample.json"))
            throw std::runtime_error("Recording manifest or truncated sample is missing");
        Apartment apartment;
        ComPtr<IWICImagingFactory> factory;
        checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&factory)), "Self-test WIC factory");
        ComPtr<IWICBitmapDecoder> decoder;
        checked(factory->CreateDecoderFromFilename((first / "frame-000000.jpg").c_str(), nullptr,
                                                    GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder), "Decode sample JPEG");
        ComPtr<IWICBitmapFrameDecode> frame;
        checked(decoder->GetFrame(0, &frame), "Decode JPEG frame");
        UINT width{}, height{};
        checked(frame->GetSize(&width, &height), "Check decoded JPEG size");
        if (width != 64 || height != 36) throw std::runtime_error("JPEG dimensions changed");
        ComPtr<IWICFormatConverter> converter;
        checked(factory->CreateFormatConverter(&converter), "Create JPEG check converter");
        checked(converter->Initialize(frame.Get(), GUID_WICPixelFormat24bppBGR,
                                      WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert sample JPEG");
        std::vector<std::uint8_t> decoded(static_cast<std::size_t>(width) * height * 3);
        checked(converter->CopyPixels(nullptr, width * 3, static_cast<UINT>(decoded.size()), decoded.data()), "Read decoded sample pixels");
        if (std::abs(static_cast<int>(decoded[0]) - 12) > 8 ||
            std::abs(static_cast<int>(decoded[1]) - 90) > 8 ||
            std::abs(static_cast<int>(decoded[2]) - 220) > 8)
            throw std::runtime_error("JPEG color channels failed round-trip");
        detail = "WIC JPEG color round-trip, queue bound and unreviewed truncated manifests verified";
        return true;
    } catch (const std::exception& error) { detail = error.what(); return false; }
}
}
