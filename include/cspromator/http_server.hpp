#pragma once

#include "cspromator/clock.hpp"
#include "cspromator/telemetry_ingress.hpp"

#include <atomic>
#include <cstdint>

namespace cspromator {

class GsiHttpServer {
public:
    GsiHttpServer(std::uint16_t port,
                  const MonotonicClock& clock,
                  TelemetryIngress& ingress);

    int run();
    void request_stop();

private:
    std::uint16_t port_;
    const MonotonicClock& clock_;
    TelemetryIngress& ingress_;
    std::atomic_bool stop_requested_{false};
};

} // namespace cspromator
