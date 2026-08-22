#include "cspromator/session.hpp"

#include <algorithm>
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
                if (static_cast<unsigned char>(ch) < 0x20) out += '?';
                else out += ch;
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

std::string optional_int(const std::optional<int>& value) {
    return value ? std::to_string(*value) : std::string{};
}

std::string optional_u64(const std::optional<std::uint64_t>& value) {
    return value ? std::to_string(*value) : std::string{};
}

std::optional<int> parse_optional_int(const std::string& value) {
    if (value.empty()) return std::nullopt;
    return std::stoi(value);
}

std::optional<std::uint64_t> parse_optional_u64(const std::string& value) {
    if (value.empty()) return std::nullopt;
    return std::stoull(value);
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
    if (!meta) throw std::runtime_error("Could not create session metadata.json");
    meta << "{\n"
         << "  \"schema_version\": 3,\n"
         << "  \"application\": \"CSPromator Probe\",\n"
         << "  \"application_version\": \"0.0.7\",\n"
         << "  \"storage\": \"packed-raw-v1\",\n"
         << "  \"supplementary_storage\": \"tsv-v1\",\n"
         << "  \"clock\": {\"name\": \"" << json_escape(info.name)
         << "\", \"frequency\": " << info.frequency << "},\n"
         << "  \"start_tick\": " << start_tick_ << "\n"
         << "}\n";

    timeline_.open(directory_ / "timeline.tsv", std::ios::binary);
    if (!timeline_) throw std::runtime_error("Could not create session timeline.tsv");
    timeline_ << "ingress_order\tsequence\taccepted_tick\tbody_complete_tick\tack_sent_tick\tpersist_complete_tick\trelative_us\tbody_offset\tbody_bytes\n";
    timeline_.flush();

    supplementary_timeline_.open(directory_ / "supplementary.timeline.tsv", std::ios::binary);
    if (!supplementary_timeline_) {
        throw std::runtime_error("Could not create session supplementary.timeline.tsv");
    }
    supplementary_timeline_
        << "ingress_order\tsequence\tobserved_tick\tpersist_complete_tick\trelative_us\tsource\troster_revision\tct_alive\tt_alive\tct_total\tt_total\n";
    supplementary_timeline_.flush();

    raw_stream_.open(directory_ / "raw.gsi", std::ios::binary | std::ios::trunc);
    if (!raw_stream_) throw std::runtime_error("Could not create session raw.gsi");

    worker_ = std::thread(&SessionRecorder::worker_loop, this);
}

SessionRecorder::~SessionRecorder() {
    try { stop_and_flush(); } catch (...) {}
}

SnapshotRecord SessionRecorder::enqueue(std::uint64_t accepted_tick,
                                        std::uint64_t body_complete_tick,
                                        std::uint64_t ack_sent_tick,
                                        std::string body) {
    SnapshotRecord record{};
    record.accepted_tick = accepted_tick;
    record.body_complete_tick = body_complete_tick;
    record.ack_sent_tick = ack_sent_tick;
    record.relative_us = static_cast<std::uint64_t>(
        clock_.seconds_between(start_tick_, body_complete_tick) * 1'000'000.0);
    record.body_bytes = body.size();

    {
        std::lock_guard lock(queue_mutex_);
        if (worker_error_) std::rethrow_exception(worker_error_);
        if (stopping_) throw std::runtime_error("Session recorder is stopping");
        record.sequence = ++sequence_;
        record.ingress_order = ++ingress_order_;
        queue_.push(PendingSnapshot{record, std::move(body)});
    }
    queue_cv_.notify_one();
    return record;
}

SupplementaryRecord SessionRecorder::enqueue_supplementary(SupplementarySnapshot snapshot) {
    if (!valid_team_counts(snapshot.teams)) {
        throw std::invalid_argument("Invalid supplementary team counts");
    }
    if (snapshot.observed_tick < start_tick_) {
        throw std::invalid_argument("Supplementary observed_tick predates session start");
    }

    SupplementaryRecord record{};
    snapshot.relative_us = static_cast<std::uint64_t>(
        clock_.seconds_between(start_tick_, snapshot.observed_tick) * 1'000'000.0);

    {
        std::lock_guard lock(queue_mutex_);
        if (worker_error_) std::rethrow_exception(worker_error_);
        if (stopping_) throw std::runtime_error("Session recorder is stopping");
        snapshot.sequence = ++supplementary_sequence_;
        record.ingress_order = ++ingress_order_;
        record.snapshot = std::move(snapshot);
        queue_.push(PendingSupplementary{record});
    }
    queue_cv_.notify_one();
    return record;
}

void SessionRecorder::stop_and_flush() {
    {
        std::lock_guard lock(queue_mutex_);
        if (stopped_) return;
        stopping_ = true;
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    stopped_ = true;
    if (worker_error_) std::rethrow_exception(worker_error_);
}

void SessionRecorder::worker_loop() {
    try {
        for (;;) {
            PendingRecord pending;
            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) {
                    if (stopping_) break;
                    continue;
                }
                pending = std::move(queue_.front());
                queue_.pop();
            }
            persist(std::move(pending));
        }
        raw_stream_.flush();
        timeline_.flush();
        supplementary_timeline_.flush();
    } catch (...) {
        std::lock_guard lock(queue_mutex_);
        worker_error_ = std::current_exception();
        stopping_ = true;
        while (!queue_.empty()) queue_.pop();
    }
}

void SessionRecorder::persist(PendingRecord pending) {
    if (std::holds_alternative<PendingSnapshot>(pending)) {
        persist_snapshot(std::get<PendingSnapshot>(std::move(pending)));
    } else {
        persist_supplementary(std::get<PendingSupplementary>(std::move(pending)));
    }
}

void SessionRecorder::persist_snapshot(PendingSnapshot pending) {
    auto& record = pending.record;
    record.body_offset = raw_offset_;

    raw_stream_.write(pending.body.data(), static_cast<std::streamsize>(pending.body.size()));
    raw_stream_.flush();
    if (!raw_stream_) throw std::runtime_error("Could not append raw GSI snapshot");

    raw_offset_ += static_cast<std::uint64_t>(pending.body.size());
    record.persist_complete_tick = clock_.now();

    timeline_ << record.ingress_order << '\t'
              << record.sequence << '\t'
              << record.accepted_tick << '\t'
              << record.body_complete_tick << '\t'
              << record.ack_sent_tick << '\t'
              << record.persist_complete_tick << '\t'
              << record.relative_us << '\t'
              << record.body_offset << '\t'
              << record.body_bytes << '\n';
    timeline_.flush();
}

void SessionRecorder::persist_supplementary(PendingSupplementary pending) {
    auto& record = pending.record;
    record.persist_complete_tick = clock_.now();
    const auto& snapshot = record.snapshot;

    supplementary_timeline_ << record.ingress_order << '\t'
                            << snapshot.sequence << '\t'
                            << snapshot.observed_tick << '\t'
                            << record.persist_complete_tick << '\t'
                            << snapshot.relative_us << '\t'
                            << to_string(snapshot.source) << '\t'
                            << optional_u64(snapshot.roster_revision) << '\t'
                            << optional_int(snapshot.teams.ct_alive) << '\t'
                            << optional_int(snapshot.teams.t_alive) << '\t'
                            << optional_int(snapshot.teams.ct_total) << '\t'
                            << optional_int(snapshot.teams.t_total) << '\n';
    supplementary_timeline_.flush();
    if (!supplementary_timeline_) {
        throw std::runtime_error("Could not append supplementary snapshot");
    }
}

std::vector<ReplayEntry> load_timeline(const std::filesystem::path& session_directory) {
    std::ifstream input(session_directory / "timeline.tsv", std::ios::binary);
    if (!input) throw std::runtime_error("Could not open timeline.tsv in " + session_directory.string());

    std::string header;
    std::getline(input, header);
    const bool packed_v2 = header.find("body_offset") != std::string::npos;
    const bool has_ingress_order = header.rfind("ingress_order\t", 0) == 0;

    std::vector<ReplayEntry> entries;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto fields = split_tsv(line);
        const std::size_t expected = has_ingress_order ? 9 : 8;
        if (fields.size() < expected) {
            throw std::runtime_error("Malformed timeline row in " + session_directory.string());
        }

        SnapshotRecord record{};
        std::size_t i = 0;
        if (has_ingress_order) record.ingress_order = std::stoull(fields[i++]);
        record.sequence = std::stoull(fields[i++]);
        if (!has_ingress_order) record.ingress_order = record.sequence;
        record.accepted_tick = std::stoull(fields[i++]);
        record.body_complete_tick = std::stoull(fields[i++]);
        record.ack_sent_tick = std::stoull(fields[i++]);
        record.persist_complete_tick = std::stoull(fields[i++]);
        record.relative_us = std::stoull(fields[i++]);

        if (packed_v2) {
            record.body_offset = std::stoull(fields[i++]);
            record.body_bytes = static_cast<std::size_t>(std::stoull(fields[i++]));
        } else {
            record.body_bytes = static_cast<std::size_t>(std::stoull(fields[i++]));
            record.body_filename = fields[i++];
        }
        entries.push_back({record, session_directory, packed_v2});
    }
    return entries;
}

std::vector<SupplementaryReplayEntry> load_supplementary_timeline(
    const std::filesystem::path& session_directory) {
    const auto path = session_directory / "supplementary.timeline.tsv";
    if (!std::filesystem::exists(path)) return {};

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open supplementary.timeline.tsv in " + session_directory.string());
    }

    std::string header;
    std::getline(input, header);
    const std::string expected_header =
        "ingress_order\tsequence\tobserved_tick\tpersist_complete_tick\trelative_us\tsource\troster_revision\tct_alive\tt_alive\tct_total\tt_total";
    if (header != expected_header) {
        throw std::runtime_error("Unsupported supplementary timeline schema in " + session_directory.string());
    }

    std::vector<SupplementaryReplayEntry> entries;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto fields = split_tsv(line);
        if (fields.size() < 11) {
            throw std::runtime_error("Malformed supplementary timeline row in " + session_directory.string());
        }

        SupplementaryReplayEntry entry{};
        entry.record.ingress_order = std::stoull(fields[0]);
        entry.record.snapshot.sequence = std::stoull(fields[1]);
        entry.record.snapshot.observed_tick = std::stoull(fields[2]);
        entry.record.persist_complete_tick = std::stoull(fields[3]);
        entry.record.snapshot.relative_us = std::stoull(fields[4]);

        const auto recorded_source = supplementary_source_from_string(fields[5]);
        if (!recorded_source) {
            throw std::runtime_error("Unknown supplementary source in " + session_directory.string());
        }
        entry.recorded_source = *recorded_source;
        entry.record.snapshot.source = SupplementarySourceKind::Replay;
        entry.record.snapshot.roster_revision = parse_optional_u64(fields[6]);
        entry.record.snapshot.teams.ct_alive = parse_optional_int(fields[7]);
        entry.record.snapshot.teams.t_alive = parse_optional_int(fields[8]);
        entry.record.snapshot.teams.ct_total = parse_optional_int(fields[9]);
        entry.record.snapshot.teams.t_total = parse_optional_int(fields[10]);

        if (!valid_team_counts(entry.record.snapshot.teams)) {
            throw std::runtime_error("Invalid supplementary team counts in " + session_directory.string());
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<ReplayItem> load_replay_stream(const std::filesystem::path& session_directory) {
    auto gsi_entries = load_timeline(session_directory);
    auto supplementary_entries = load_supplementary_timeline(session_directory);

    std::vector<ReplayItem> items;
    items.reserve(gsi_entries.size() + supplementary_entries.size());

    for (auto& entry : gsi_entries) {
        ReplayItem item{};
        item.kind = ReplayItemKind::Gsi;
        item.ingress_order = entry.record.ingress_order;
        item.relative_us = entry.record.relative_us;
        item.gsi = std::move(entry);
        items.push_back(std::move(item));
    }
    for (auto& entry : supplementary_entries) {
        ReplayItem item{};
        item.kind = ReplayItemKind::Supplementary;
        item.ingress_order = entry.record.ingress_order;
        item.relative_us = entry.record.snapshot.relative_us;
        item.supplementary = std::move(entry);
        items.push_back(std::move(item));
    }

    // Schema-v3 ingress order is the authoritative order the live semantic
    // pipeline received. Observation timestamps remain data, not a license to
    // reorder delayed cross-source submissions during replay.
    std::stable_sort(items.begin(), items.end(), [](const ReplayItem& a, const ReplayItem& b) {
        if (a.ingress_order != b.ingress_order) return a.ingress_order < b.ingress_order;
        if (a.relative_us != b.relative_us) return a.relative_us < b.relative_us;
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    return items;
}

std::string load_replay_body(const ReplayEntry& entry) {
    if (!entry.packed_v2) {
        return load_text_file(entry.session_directory / "raw" / entry.record.body_filename);
    }

    std::ifstream input(entry.session_directory / "raw.gsi", std::ios::binary);
    if (!input) throw std::runtime_error("Could not open raw.gsi in " + entry.session_directory.string());
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
    if (!input) throw std::runtime_error("Could not open " + path.string());
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

} // namespace cspromator
