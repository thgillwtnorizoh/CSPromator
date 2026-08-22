#include "cspromator/clock.hpp"

#include <chrono>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace cspromator {

MonotonicClock::MonotonicClock() {
#ifdef _WIN32
    LARGE_INTEGER freq{};
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) {
        throw std::runtime_error("QueryPerformanceFrequency failed");
    }
    frequency_ = static_cast<std::uint64_t>(freq.QuadPart);
#else
    frequency_ = 1'000'000'000ULL;
#endif
}

std::uint64_t MonotonicClock::now() const {
#ifdef _WIN32
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<std::uint64_t>(counter.QuadPart);
#else
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    return static_cast<std::uint64_t>(ns.count());
#endif
}

ClockInfo MonotonicClock::info() const {
#ifdef _WIN32
    return {"QueryPerformanceCounter", frequency_};
#else
    return {"std::chrono::steady_clock(test-host)", frequency_};
#endif
}

double MonotonicClock::seconds_between(std::uint64_t earlier, std::uint64_t later) const {
    if (later < earlier) {
        return 0.0;
    }
    return static_cast<double>(later - earlier) / static_cast<double>(frequency_);
}

} // namespace cspromator
