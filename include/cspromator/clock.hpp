#pragma once

#include <cstdint>
#include <string>

namespace cspromator {

struct ClockInfo {
    std::string name;
    std::uint64_t frequency;
};

class MonotonicClock {
public:
    MonotonicClock();
    [[nodiscard]] std::uint64_t now() const;
    [[nodiscard]] ClockInfo info() const;
    [[nodiscard]] double seconds_between(std::uint64_t earlier, std::uint64_t later) const;

private:
    std::uint64_t frequency_{};
};

} // namespace cspromator
