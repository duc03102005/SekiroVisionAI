#include <SekiroVisionAI/Timing.h>
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
