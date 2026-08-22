#include "cspromator/live_event_pipeline.hpp"

#include <stdexcept>
#include <utility>

namespace cspromator {

LiveEventPipeline::LiveEventPipeline(BatchHandler handler)
    : handler_(std::move(handler)), worker_(&LiveEventPipeline::worker_loop, this) {}

LiveEventPipeline::~LiveEventPipeline() {
    try {
        stop_and_flush();
    } catch (...) {
    }
}

void LiveEventPipeline::enqueue(std::uint64_t sequence,
                                std::uint64_t relative_us,
                                std::string body) {
    std::lock_guard lock(queue_mutex_);
    if (stopping_ || stopped_) {
        throw std::runtime_error("Live event pipeline is stopping");
    }
    if (worker_error_) {
        std::rethrow_exception(worker_error_);
    }

    PendingItem item;
    item.kind = PendingItem::Kind::Gsi;
    item.gsi = PendingPayload{sequence, relative_us, std::move(body)};
    queue_.push(std::move(item));
    queue_cv_.notify_one();
}

void LiveEventPipeline::enqueue_supplementary(SupplementarySnapshot snapshot) {
    std::lock_guard lock(queue_mutex_);
    if (stopping_ || stopped_) {
        throw std::runtime_error("Live event pipeline is stopping");
    }
    if (worker_error_) {
        std::rethrow_exception(worker_error_);
    }

    PendingItem item;
    item.kind = PendingItem::Kind::Supplementary;
    item.supplementary = std::move(snapshot);
    queue_.push(std::move(item));
    queue_cv_.notify_one();
}

void LiveEventPipeline::stop_and_flush() {
    {
        std::lock_guard lock(queue_mutex_);
        if (stopped_) {
            if (worker_error_) {
                std::rethrow_exception(worker_error_);
            }
            return;
        }
        stopping_ = true;
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    stopped_ = true;
    if (worker_error_) {
        std::rethrow_exception(worker_error_);
    }
}

void LiveEventPipeline::worker_loop() {
    try {
        for (;;) {
            PendingItem pending;
            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait(lock, [this] {
                    return stopping_ || !queue_.empty();
                });
                if (queue_.empty()) {
                    if (stopping_) {
                        break;
                    }
                    continue;
                }
                pending = std::move(queue_.front());
                queue_.pop();
            }

            if (pending.kind == PendingItem::Kind::Supplementary) {
                auto semantic_events = resolver_.process_supplementary(pending.supplementary);
                if (!semantic_events.empty() && handler_ && latest_state_) {
                    handler_(*latest_state_, semantic_events);
                }
                continue;
            }

            auto state = normalize_gsi(
                pending.gsi.body,
                pending.gsi.sequence,
                pending.gsi.relative_us);
            if (!state.payload_valid) {
                continue;
            }

            auto events = detector_.process(state);
            latest_state_ = state;
            auto semantic_events = resolver_.process_gsi(state, events);
            events.insert(events.end(), semantic_events.begin(), semantic_events.end());

            if (!events.empty() && handler_) {
                handler_(state, events);
            }
        }
    } catch (...) {
        std::lock_guard lock(queue_mutex_);
        worker_error_ = std::current_exception();
        stopping_ = true;
    }
}

} // namespace cspromator
