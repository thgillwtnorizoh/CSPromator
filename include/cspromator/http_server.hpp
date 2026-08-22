#pragma once

#include "cspromator/clock.hpp"
#include "cspromator/session.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>

namespace cspromator {

class GsiHttpServer {
public:
    GsiHttpServer(std::uint16_t port,
                  const MonotonicClock& clock,
                  SessionRecorder& recorder);

    int run();
    void request_stop();

private:
    std::uint16_t port_;
    const MonotonicClock& clock_;
    SessionRecorder& recorder_;
    std::atomic_bool stop_requested_{false};
};

} // namespace cspromator
