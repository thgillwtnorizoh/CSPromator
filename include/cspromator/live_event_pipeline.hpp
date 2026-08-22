#pragma once

#include "cspromator/event_detector.hpp"
#include "cspromator/game_state.hpp"
#include "cspromator/semantic_resolver.hpp"
#include "cspromator/supplementary_state.hpp"

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace cspromator {

class LiveEventPipeline {
public:
    using BatchHandler = std::function<void(const NormalizedGameState&,
                                            const std::vector<PromatorEvent>&)>;

    explicit LiveEventPipeline(BatchHandler handler);
    ~LiveEventPipeline();

    LiveEventPipeline(const LiveEventPipeline&) = delete;
    LiveEventPipeline& operator=(const LiveEventPipeline&) = delete;

    // Existing GSI ingress. Its behavior remains unchanged when no
    // supplementary snapshots are submitted.
    void enqueue(std::uint64_t sequence,
                 std::uint64_t relative_us,
                 std::string body);

    // Optional second ingress for player-visible aggregate evidence. A future
    // provider timestamps observations with the same monotonic clock and pushes
    // them here. The core never polls Panorama itself.
    void enqueue_supplementary(SupplementarySnapshot snapshot);

    void stop_and_flush();

private:
    struct PendingPayload {
        std::uint64_t sequence{};
        std::uint64_t relative_us{};
        std::string body;
    };

    struct PendingItem {
        enum class Kind {
            Gsi,
            Supplementary,
        };

        Kind kind{Kind::Gsi};
        PendingPayload gsi;
        SupplementarySnapshot supplementary;
    };

    void worker_loop();

    BatchHandler handler_;
    EventDetector detector_;
    SemanticResolver resolver_;
    std::optional<NormalizedGameState> latest_state_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<PendingItem> queue_;
    bool stopping_{false};
    bool stopped_{false};
    std::exception_ptr worker_error_;
    std::thread worker_;
};

} // namespace cspromator
