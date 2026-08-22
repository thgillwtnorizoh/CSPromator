#pragma once

#include "cspromator/clock.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace cspromator {

struct SnapshotRecord {
    std::uint64_t sequence{};
    std::uint64_t accepted_tick{};
    std::uint64_t body_complete_tick{};
    std::uint64_t ack_sent_tick{};
    std::uint64_t persist_complete_tick{};
    std::uint64_t relative_us{};
    std::uint64_t body_offset{};
    std::size_t body_bytes{};
    std::string body_filename; // v1 compatibility only
};

class SessionRecorder {
public:
    SessionRecorder(const std::filesystem::path& sessions_root, const MonotonicClock& clock);
    ~SessionRecorder();

    SessionRecorder(const SessionRecorder&) = delete;
    SessionRecorder& operator=(const SessionRecorder&) = delete;

    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }
    [[nodiscard]] std::uint64_t start_tick() const { return start_tick_; }

    // Queue a snapshot for persistence. This method performs no filesystem I/O.
    SnapshotRecord enqueue(std::uint64_t accepted_tick,
                           std::uint64_t body_complete_tick,
                           std::uint64_t ack_sent_tick,
                           std::string body);

    // Drain queued snapshots and stop the persistence worker.
    void stop_and_flush();

private:
    struct PendingSnapshot {
        SnapshotRecord record;
        std::string body;
    };

    void worker_loop();
    void persist(PendingSnapshot pending);

    const MonotonicClock& clock_;
    std::filesystem::path directory_;
    std::ofstream timeline_;
    std::ofstream raw_stream_;
    std::uint64_t start_tick_{};
    std::uint64_t sequence_{};
    std::uint64_t raw_offset_{};

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<PendingSnapshot> queue_;
    bool stopping_{false};
    bool stopped_{false};
    std::exception_ptr worker_error_;
    std::thread worker_;
};

struct ReplayEntry {
    SnapshotRecord record;
    std::filesystem::path session_directory;
    bool packed_v2{false};
};

std::vector<ReplayEntry> load_timeline(const std::filesystem::path& session_directory);
std::string load_replay_body(const ReplayEntry& entry);
std::string load_text_file(const std::filesystem::path& path);

} // namespace cspromator
