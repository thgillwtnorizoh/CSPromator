#pragma once

#include "cspromator/clock.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace cspromator {

struct SnapshotRecord {
    std::uint64_t sequence{};
    std::uint64_t accepted_tick{};
    std::uint64_t body_complete_tick{};
    std::uint64_t relative_us{};
    std::size_t body_bytes{};
    std::string body_filename;
};

class SessionRecorder {
public:
    SessionRecorder(const std::filesystem::path& sessions_root, const MonotonicClock& clock);

    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }
    [[nodiscard]] std::uint64_t start_tick() const { return start_tick_; }

    SnapshotRecord append(std::uint64_t accepted_tick,
                          std::uint64_t body_complete_tick,
                          const std::string& body);

private:
    const MonotonicClock& clock_;
    std::filesystem::path directory_;
    std::filesystem::path raw_directory_;
    std::ofstream timeline_;
    std::uint64_t start_tick_{};
    std::uint64_t sequence_{};
};

struct ReplayEntry {
    SnapshotRecord record;
    std::filesystem::path body_path;
};

std::vector<ReplayEntry> load_timeline(const std::filesystem::path& session_directory);
std::string load_text_file(const std::filesystem::path& path);

} // namespace cspromator
