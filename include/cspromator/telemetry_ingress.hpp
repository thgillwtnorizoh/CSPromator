#pragma once

#include "cspromator/live_event_pipeline.hpp"
#include "cspromator/session.hpp"
#include "cspromator/supplementary_state.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace cspromator {

// One tiny serialization point sits in front of persistence + live semantics.
// It performs no parsing or filesystem I/O. Its job is to guarantee that the
// cross-source order recorded in schema-v3 sessions is the same order submitted
// to the live event pipeline.
class TelemetryIngress {
public:
    TelemetryIngress(SessionRecorder& recorder, LiveEventPipeline& live_events);

    SnapshotRecord enqueue_gsi(std::uint64_t accepted_tick,
                               std::uint64_t body_complete_tick,
                               std::uint64_t ack_sent_tick,
                               std::string body);

    SupplementaryRecord enqueue_supplementary(SupplementarySnapshot snapshot);

    [[nodiscard]] const std::filesystem::path& session_directory() const {
        return recorder_.directory();
    }

private:
    SessionRecorder& recorder_;
    LiveEventPipeline& live_events_;
    std::mutex mutex_;
};

} // namespace cspromator
