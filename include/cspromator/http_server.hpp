#pragma once

#include "cspromator/clock.hpp"
#include "cspromator/live_event_pipeline.hpp"
#include "cspromator/session.hpp"

#include <atomic>
#include <cstdint>

namespace cspromator {

class GsiHttpServer {
public:
    GsiHttpServer(std::uint16_t port,
                  const MonotonicClock& clock,
                  SessionRecorder& recorder,
                  LiveEventPipeline& live_events);

    int run();
    void request_stop();

private:
    std::uint16_t port_;
    const MonotonicClock& clock_;
    SessionRecorder& recorder_;
    LiveEventPipeline& live_events_;
    std::atomic_bool stop_requested_{false};
};

} // namespace cspromator
