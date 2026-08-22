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

std::vector<std::string> split_tsv(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const auto end = line.find('\t', begin);
        if (end == std::string::npos) {
            fields.push_back(line.substr(begin));
            break;
        }
        fields.push_back(line.substr(begin, end - begin));
        begin = end + 1;
    }
    return fields;
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
    std::filesystem::create_directories(directory_);

    const auto info = clock_.info();
    std::ofstream meta(directory_ / "metadata.json", std::ios::binary);
    if (!meta) {
        throw std::runtime_error("Could not create session metadata.json");
    }
    meta << "{\n"
         << "  \"schema_version\": 2,\n"
         << "  \"application\": \"CSPromator Probe\",\n"
         << "  \"application_version\": \"0.0.5\",\n"
         << "  \"storage\": \"packed-raw-v1\",\n"
         << "  \"clock\": {\"name\": \"" << json_escape(info.name)
         << "\", \"frequency\": " << info.frequency << "},\n"
         << "  \"start_tick\": " << start_tick_ << "\n"
         << "}\n";

    timeline_.open(directory_ / "timeline.tsv", std::ios::binary);
    if (!timeline_) {
        throw std::runtime_error("Could not create session timeline.tsv");
    }
    timeline_ << "sequence\taccepted_tick\tbody_complete_tick\tack_sent_tick\tpersist_complete_tick\trelative_us\tbody_offset\tbody_bytes\n";
    timeline_.flush();

    raw_stream_.open(directory_ / "raw.gsi", std::ios::binary | std::ios::trunc);
    if (!raw_stream_) {
        throw std::runtime_error("Could not create session raw.gsi");
    }

    worker_ = std::thread(&SessionRecorder::worker_loop, this);
}

SessionRecorder::~SessionRecorder() {
    try {
        stop_and_flush();
    } catch (...) {
    }
}

SnapshotRecord SessionRecorder::enqueue(std::uint64_t accepted_tick,
                                        std::uint64_t body_complete_tick,
                                        std::uint64_t ack_sent_tick,
                                        std::string body) {
    SnapshotRecord record{};
    record.sequence = ++sequence_;
    record.accepted_tick = accepted_tick;
    record.body_complete_tick = body_complete_tick;
    record.ack_sent_tick = ack_sent_tick;
    record.relative_us = static_cast<std::uint64_t>(
        clock_.seconds_between(start_tick_, body_complete_tick) * 1'000'000.0);
    record.body_bytes = body.size();

    {
        std::lock_guard lock(queue_mutex_);
        if (worker_error_) {
            std::rethrow_exception(worker_error_);
        }
        if (stopping_) {
            throw std::runtime_error("Session recorder is stopping");
        }
        queue_.push(PendingSnapshot{record, std::move(body)});
    }
    queue_cv_.notify_one();
    return record;
}

void SessionRecorder::stop_and_flush() {
    {
        std::lock_guard lock(queue_mutex_);
        if (stopped_) {
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

void SessionRecorder::worker_loop() {
    try {
        for (;;) {
            PendingSnapshot pending;
            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) {
                    if (stopping_) {
                        break;
                    }
                    continue;
                }
                pending = std::move(queue_.front());
                queue_.pop();
            }
            persist(std::move(pending));
        }

        raw_stream_.flush();
        timeline_.flush();
    } catch (...) {
        std::lock_guard lock(queue_mutex_);
        worker_error_ = std::current_exception();
        stopping_ = true;
        while (!queue_.empty()) {
            queue_.pop();
        }
    }
}

void SessionRecorder::persist(PendingSnapshot pending) {
    auto& record = pending.record;
    record.body_offset = raw_offset_;

    raw_stream_.write(pending.body.data(), static_cast<std::streamsize>(pending.body.size()));
    raw_stream_.flush();
    if (!raw_stream_) {
        throw std::runtime_error("Could not append raw GSI snapshot");
    }

    raw_offset_ += static_cast<std::uint64_t>(pending.body.size());
    record.persist_complete_tick = clock_.now();

    timeline_ << record.sequence << '\t'
              << record.accepted_tick << '\t'
              << record.body_complete_tick << '\t'
              << record.ack_sent_tick << '\t'
              << record.persist_complete_tick << '\t'
              << record.relative_us << '\t'
              << record.body_offset << '\t'
              << record.body_bytes << '\n';
    timeline_.flush();
}

std::vector<ReplayEntry> load_timeline(const std::filesystem::path& session_directory) {
    std::ifstream input(session_directory / "timeline.tsv", std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open timeline.tsv in " + session_directory.string());
    }

    std::string header;
    std::getline(input, header);
    const bool packed_v2 = header.find("body_offset") != std::string::npos;

    std::vector<ReplayEntry> entries;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_tsv(line);
        if ((packed_v2 && fields.size() < 8) || (!packed_v2 && fields.size() < 8)) {
            throw std::runtime_error("Malformed timeline row in " + session_directory.string());
        }

        SnapshotRecord record{};
        record.sequence = std::stoull(fields[0]);
        record.accepted_tick = std::stoull(fields[1]);
        record.body_complete_tick = std::stoull(fields[2]);
        record.ack_sent_tick = std::stoull(fields[3]);
        record.persist_complete_tick = std::stoull(fields[4]);
        record.relative_us = std::stoull(fields[5]);

        if (packed_v2) {
            record.body_offset = std::stoull(fields[6]);
            record.body_bytes = static_cast<std::size_t>(std::stoull(fields[7]));
        } else {
            record.body_bytes = static_cast<std::size_t>(std::stoull(fields[6]));
            record.body_filename = fields[7];
        }

        entries.push_back({record, session_directory, packed_v2});
    }
    return entries;
}

std::string load_replay_body(const ReplayEntry& entry) {
    if (!entry.packed_v2) {
        return load_text_file(entry.session_directory / "raw" / entry.record.body_filename);
    }

    std::ifstream input(entry.session_directory / "raw.gsi", std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open raw.gsi in " + entry.session_directory.string());
    }
    input.seekg(static_cast<std::streamoff>(entry.record.body_offset));
    std::string body(entry.record.body_bytes, '\0');
    input.read(body.data(), static_cast<std::streamsize>(body.size()));
    if (input.gcount() != static_cast<std::streamsize>(body.size())) {
        throw std::runtime_error("Short read from raw.gsi");
    }
    return body;
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
