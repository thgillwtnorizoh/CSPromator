#include "cspromator/session.hpp"

#include <chrono>
#include <format>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cspromator {
namespace {

std::string make_session_name() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << "session_" << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (const char ch : input) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out += '?';
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

} // namespace

SessionRecorder::SessionRecorder(const std::filesystem::path& sessions_root,
                                 const MonotonicClock& clock)
    : clock_(clock), start_tick_(clock.now()) {
    std::filesystem::create_directories(sessions_root);

    directory_ = sessions_root / make_session_name();
    for (unsigned suffix = 1; std::filesystem::exists(directory_); ++suffix) {
        directory_ = sessions_root / (make_session_name() + "_" + std::to_string(suffix));
    }

    raw_directory_ = directory_ / "raw";
    std::filesystem::create_directories(raw_directory_);

    const auto info = clock_.info();
    std::ofstream meta(directory_ / "metadata.json", std::ios::binary);
    if (!meta) {
        throw std::runtime_error("Could not create session metadata.json");
    }
    meta << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"application\": \"CSPromator Probe\",\n"
         << "  \"application_version\": \"0.0.1\",\n"
         << "  \"clock\": {\"name\": \"" << json_escape(info.name)
         << "\", \"frequency\": " << info.frequency << "},\n"
         << "  \"start_tick\": " << start_tick_ << "\n"
         << "}\n";

    timeline_.open(directory_ / "timeline.tsv", std::ios::binary);
    if (!timeline_) {
        throw std::runtime_error("Could not create session timeline.tsv");
    }
    timeline_ << "sequence\taccepted_tick\tbody_complete_tick\tack_sent_tick\tpersist_complete_tick\trelative_us\tbody_bytes\tbody_file\n";
    timeline_.flush();
}

SnapshotRecord SessionRecorder::append(std::uint64_t accepted_tick,
                                       std::uint64_t body_complete_tick,
                                       std::uint64_t ack_sent_tick,
                                       const std::string& body) {
    SnapshotRecord record{};
    record.sequence = ++sequence_;
    record.accepted_tick = accepted_tick;
    record.body_complete_tick = body_complete_tick;
    record.ack_sent_tick = ack_sent_tick;
    record.relative_us = static_cast<std::uint64_t>(
        clock_.seconds_between(start_tick_, body_complete_tick) * 1'000'000.0);
    record.body_bytes = body.size();
    record.body_filename = std::format("{:08}.json", record.sequence);

    const auto raw_path = raw_directory_ / record.body_filename;
    std::ofstream raw(raw_path, std::ios::binary);
    if (!raw) {
        throw std::runtime_error("Could not create raw snapshot file: " + raw_path.string());
    }
    raw.write(body.data(), static_cast<std::streamsize>(body.size()));
    raw.flush();
    record.persist_complete_tick = clock_.now();

    timeline_ << record.sequence << '\t'
              << record.accepted_tick << '\t'
              << record.body_complete_tick << '\t'
              << record.ack_sent_tick << '\t'
              << record.persist_complete_tick << '\t'
              << record.relative_us << '\t'
              << record.body_bytes << '\t'
              << record.body_filename << '\n';
    timeline_.flush();
    return record;
}

std::vector<ReplayEntry> load_timeline(const std::filesystem::path& session_directory) {
    std::ifstream input(session_directory / "timeline.tsv", std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open timeline.tsv in " + session_directory.string());
    }

    std::vector<ReplayEntry> entries;
    std::string line;
    std::getline(input, line); // header
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream row(line);
        std::string field;
        SnapshotRecord record{};

        std::getline(row, field, '\t'); record.sequence = std::stoull(field);
        std::getline(row, field, '\t'); record.accepted_tick = std::stoull(field);
        std::getline(row, field, '\t'); record.body_complete_tick = std::stoull(field);
        std::getline(row, field, '\t'); record.ack_sent_tick = std::stoull(field);
        std::getline(row, field, '\t'); record.persist_complete_tick = std::stoull(field);
        std::getline(row, field, '\t'); record.relative_us = std::stoull(field);
        std::getline(row, field, '\t'); record.body_bytes = static_cast<std::size_t>(std::stoull(field));
        std::getline(row, record.body_filename, '\t');

        entries.push_back({record, session_directory / "raw" / record.body_filename});
    }
    return entries;
}

std::string load_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open " + path.string());
    }
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

} // namespace cspromator
