#include "cspromator/telemetry_ingress.hpp"

#include <utility>

namespace cspromator {

TelemetryIngress::TelemetryIngress(SessionRecorder& recorder,
                                   LiveEventPipeline& live_events)
    : recorder_(recorder), live_events_(live_events) {}

SnapshotRecord TelemetryIngress::enqueue_gsi(std::uint64_t accepted_tick,
                                             std::uint64_t body_complete_tick,
                                             std::uint64_t ack_sent_tick,
                                             std::string body) {
    std::lock_guard lock(mutex_);

    std::string live_body = body;
    const auto record = recorder_.enqueue(
        accepted_tick, body_complete_tick, ack_sent_tick, std::move(body));
    live_events_.enqueue(record.sequence, record.relative_us, std::move(live_body));
    return record;
}

SupplementaryRecord TelemetryIngress::enqueue_supplementary(
    SupplementarySnapshot snapshot) {
    std::lock_guard lock(mutex_);

    const auto record = recorder_.enqueue_supplementary(std::move(snapshot));
    live_events_.enqueue_supplementary(record.snapshot);
    return record;
}

} // namespace cspromator
