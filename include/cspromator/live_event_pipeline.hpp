#pragma once

#include "cspromator/event_detector.hpp"
#include "cspromator/game_state.hpp"

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
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

    void enqueue(std::uint64_t sequence,
                 std::uint64_t relative_us,
                 std::string body);

    void stop_and_flush();

private:
    struct PendingPayload {
        std::uint64_t sequence{};
        std::uint64_t relative_us{};
        std::string body;
    };

    void worker_loop();

    BatchHandler handler_;
    EventDetector detector_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<PendingPayload> queue_;
    bool stopping_{false};
    bool stopped_{false};
    std::exception_ptr worker_error_;
    std::thread worker_;
};

} // namespace cspromator
