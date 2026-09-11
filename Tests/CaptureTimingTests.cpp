#include <SekiroVisionAI/Timing.h>
#include <SekiroVisionAI/CaptureDelivery.h>
#include <SekiroVisionAI/FramePixels.h>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace sekiro;
    require(age_ms(1200, 1205) == 5, "matched QPC age");
    require(std::isnan(age_ms(1205, 1200)), "future source rejected");
    require(std::isnan(age_ms(0, 1200)), "missing source rejected");
    require(std::isnan(age_ms(unavailable, 1200)), "NaN source rejected");
    // WGC's TimeSpan is not in hardware QPC ticks. Verify non-10 MHz QPC,
    // fractional milliseconds and a long-running system share one epoch.
    for (std::int64_t frequency : {3'125'000LL, 10'000'000LL, 24'000'000LL, 3'000'000'000LL}) {
        constexpr std::int64_t uptime_seconds = 90LL * 24 * 60 * 60;
        const auto counter = uptime_seconds * frequency + frequency / 4;
        const auto timespan = uptime_seconds * 10'000'000LL + 2'500'000;
        require(std::abs(qpc_ticks_ms(counter, frequency) - wgc_timespan_ms(timespan)) < 0.0001,
            "WGC and QPC retain the same source epoch across timer frequencies");
    }
    require(wgc_timespan_ms(12'345'678) == 1234.5678, "100 ns conversion preserves sub-millisecond source precision");
    require(std::isnan(qpc_ticks_ms(100, 0)) && std::isnan(wgc_timespan_ms(-1)), "invalid clock readings rejected");
    require(fresh_at(1000, 1119.999, 120) && !fresh_at(1000, 1120, 120), "strict unchanged live age budget");
    require(!fresh_at(1000, 4001, 120), "3001 ms source never relabelled fresh");
    require(!fresh_at(INFINITY, 4001, 120) && !fresh_at(unavailable, 4001, 120) &&
        !fresh_at(5000, 4001, 120) && !fresh_at(4000, 4001, unavailable), "invalid and future timestamps never arm input");

    CaptureDelivery delivery;
    // Reused slot 0 completes a newer capture before old slot 1 is observed.
    std::array<CompletedCapture, 3> completed{{{{12, 1, 1012}, true}, {{10, 1, 1010}, true}, {{11, 1, 1011}, true}}};
    auto selected = delivery.newest(completed, 1020);
    require(selected && *selected == 0, "ring physical order cannot reorder source delivery");
    require(delivery.delivered(completed[*selected].stamp, 1021), "newest completed frame delivered with its source timestamp");
    require(!delivery.newest(completed, 1022), "older completions cannot replace newest delivered frame");
    completed[0] = {{13, 1, 1030}, false};
    require(!delivery.newest(completed, 1031), "GPU completion remains mandatory even when telemetry is optional");
    completed[0].gpu_complete = true;
    require(delivery.newest(completed, 1031) == 0, "ready pixels selectable independently of profiling query readiness");
    require(!delivery.delivered(completed[0].stamp, 1150), "frame aging during CPU preprocessing is dropped before sink");
    require(delivery.delivered(completed[0].stamp, 1032), "failed freshness check does not corrupt delivery cursor");
    completed[0] = {{14, 0, 1040}, true};
    require(!delivery.newest(completed, 1041), "old capture generation is rejected");
    completed[0] = {{15, 2, 1050}, true};
    require(delivery.newest(completed, 1051) == 0, "new capture generation can resume with fresh source time");
    completed[0] = {{16, 2, 1000}, true};
    require(!delivery.newest(completed, 1051), "increasing sequence cannot mask a regressing source timestamp");

    ColorFrame padded; padded.width = 2; padded.height = 2; padded.stride = 12;
    padded.bgra = {0,0,255,255, 255,0,0,255, 255,255,255,255,
                   0,255,0,255, 255,255,255,255};
    SmallFrame shared; shared.sequence = 77; shared.generation = 3; shared.source_ms = 1234.5;
    derive_gray(padded, shared);
    require(shared.gray[0] == 76 && shared.gray[vision_width - 1] == 29 &&
        shared.gray[(vision_height - 1) * vision_width] == 150 && shared.gray.back() == 255,
        "live and replay derivative preserve BGRA channel order and padded row pitch");
    require(shared.source_ms == 1234.5 && shared.sequence == 77 && shared.generation == 3,
        "shared pixel processing never rewrites capture identity or freshness");
    padded.bgra.pop_back();
    require(!valid_color_frame(padded), "truncated BGRA row cannot enter either native path");
    Series<4> series;
    require(std::isnan(series.percentiles().p50), "empty percentile is unavailable");
    series.add(unavailable); series.add(-1); series.add(INFINITY);
    require(series.size() == 0, "invalid durations excluded");
    for (double v : {4, 1, 3, 2}) series.add(v);
    auto p = series.percentiles();
    require(p.p50 == 2 && p.p95 == 4 && p.p99 == 4, "nearest-rank percentiles");
    series.add(10); // Retires oldest value 4, not smallest value 1.
    p = series.percentiles();
    require(series.size() == 4 && p.maximum == 10 && p.p50 == 2, "bounded history wraps chronologically");
    Series<> cadence;
    for (int i = 1; i <= 120; ++i) cadence.add(10000.0 + i * 1000.0 / 60.0);
    require(cadence.rate(12000) == 60, "known 60 Hz cadence in two-second window");
    require(cadence.rate(14001) == 0, "stale source cannot keep displaying 60 FPS");
    require(std::isnan(cadence.rate(12000, 0)), "invalid rate window rejected");
    Series<4> boundary;
    boundary.add(1000); boundary.add(2000); boundary.add(3000); boundary.add(5000);
    require(boundary.rate(3000) == 1, "window is open-left, closed-right; future events excluded");
    std::cout << "Capture timing invariants: PASS\n";
}
