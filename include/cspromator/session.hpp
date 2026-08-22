#pragma once

#include "cspromator/clock.hpp"
#include "cspromator/supplementary_state.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace cspromator {

struct SnapshotRecord {
    // v3: ordering shared with supplementary records. Older sessions synthesize
    // this from their GSI sequence because they contain no supplementary stream.
    std::uint64_t ingress_order{};
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

struct SupplementaryRecord {
    std::uint64_t ingress_order{};
    std::uint64_t persist_complete_tick{};
    SupplementarySnapshot snapshot;
};

class SessionRecorder {
public:
    SessionRecorder(const std::filesystem::path& sessions_root, const MonotonicClock& clock);
    ~SessionRecorder();

    SessionRecorder(const SessionRecorder&) = delete;
    SessionRecorder& operator=(const SessionRecorder&) = delete;

    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }
    [[nodiscard]] std::uint64_t start_tick() const { return start_tick_; }

    // Queue a GSI snapshot for persistence. This method performs no filesystem I/O.
    SnapshotRecord enqueue(std::uint64_t accepted_tick,
                           std::uint64_t body_complete_tick,
                           std::uint64_t ack_sent_tick,
                           std::string body);

    // Queue a supplementary observation for persistence. The provider must stamp
    // observed_tick using the same QPC clock before calling this method. The
    // recorder assigns its source-local sequence, session-relative timestamp and
    // cross-source ingress order, then returns the exact snapshot that should be
    // passed to the live semantic pipeline.
    SupplementaryRecord enqueue_supplementary(SupplementarySnapshot snapshot);

    // Drain queued records and stop the persistence worker.
    void stop_and_flush();

private:
    struct PendingSnapshot {
        SnapshotRecord record;
        std::string body;
    };

    struct PendingSupplementary {
        SupplementaryRecord record;
    };

    using PendingRecord = std::variant<PendingSnapshot, PendingSupplementary>;

    void worker_loop();
    void persist(PendingRecord pending);
    void persist_snapshot(PendingSnapshot pending);
    void persist_supplementary(PendingSupplementary pending);

    const MonotonicClock& clock_;
    std::filesystem::path directory_;
    std::ofstream timeline_;
    std::ofstream raw_stream_;
    std::ofstream supplementary_timeline_;
    std::uint64_t start_tick_{};
    std::uint64_t sequence_{};
    std::uint64_t supplementary_sequence_{};
    std::uint64_t ingress_order_{};
    std::uint64_t raw_offset_{};

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<PendingRecord> queue_;
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

struct SupplementaryReplayEntry {
    SupplementaryRecord record;
    // The source that produced the observation during recording. The snapshot's
    // live source is Replay when loaded so consumers never mistake replayed
    // evidence for a currently connected provider.
    SupplementarySourceKind recorded_source{SupplementarySourceKind::Unknown};
};

enum class ReplayItemKind {
    Gsi,
    Supplementary,
};

struct ReplayItem {
    ReplayItemKind kind{ReplayItemKind::Gsi};
    std::uint64_t ingress_order{};
    std::uint64_t relative_us{};
    std::optional<ReplayEntry> gsi;
    std::optional<SupplementaryReplayEntry> supplementary;
};

std::vector<ReplayEntry> load_timeline(const std::filesystem::path& session_directory);
std::vector<SupplementaryReplayEntry> load_supplementary_timeline(
    const std::filesystem::path& session_directory);
std::vector<ReplayItem> load_replay_stream(const std::filesystem::path& session_directory);
std::string load_replay_body(const ReplayEntry& entry);
std::string load_text_file(const std::filesystem::path& path);

} // namespace cspromator
