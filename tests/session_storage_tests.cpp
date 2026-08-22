#include "cspromator/clock.hpp"
#include "cspromator/session.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

std::filesystem::path make_temp_root(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string("cspromator_test_") + suffix + "_" + std::to_string(stamp));
}

void test_v3_combined_roundtrip() {
    const auto root = make_temp_root("v3");
    std::filesystem::create_directories(root);

    cspromator::MonotonicClock clock;
    std::filesystem::path session;
    {
        cspromator::SessionRecorder recorder(root, clock);
        session = recorder.directory();

        const auto a = clock.now();
        const auto first = recorder.enqueue(a, a, a, R"({"n":1})");

        cspromator::SupplementarySnapshot supplement;
        supplement.observed_tick = clock.now();
        supplement.source = cspromator::SupplementarySourceKind::Synthetic;
        supplement.roster_revision = 9;
        supplement.teams.ct_alive = 3;
        supplement.teams.t_alive = 2;
        supplement.teams.ct_total = 5;
        supplement.teams.t_total = 5;
        const auto supplementary = recorder.enqueue_supplementary(supplement);

        const auto b = clock.now();
        const auto second = recorder.enqueue(b, b, b, R"({"n":2,"text":"tomato"})");

        require(first.ingress_order < supplementary.ingress_order,
                "supplement should follow first GSI in shared ingress order");
        require(supplementary.ingress_order < second.ingress_order,
                "second GSI should follow supplement in shared ingress order");
        recorder.stop_and_flush();
    }

    const auto entries = cspromator::load_timeline(session);
    require(entries.size() == 2, "v3 GSI timeline should contain two records");
    require(entries[0].packed_v2 && entries[1].packed_v2, "v3 GSI entries should keep packed storage");
    require(cspromator::load_replay_body(entries[0]) == R"({"n":1})", "first packed body should round-trip exactly");
    require(cspromator::load_replay_body(entries[1]) == R"({"n":2,"text":"tomato"})", "second packed body should round-trip exactly");

    const auto supplements = cspromator::load_supplementary_timeline(session);
    require(supplements.size() == 1, "v3 supplementary timeline should contain one record");
    require(supplements[0].recorded_source == cspromator::SupplementarySourceKind::Synthetic,
            "replay entry should preserve recorded provider source");
    require(supplements[0].record.snapshot.source == cspromator::SupplementarySourceKind::Replay,
            "loaded supplementary observation should identify itself as replay");
    require(supplements[0].record.snapshot.roster_revision &&
                *supplements[0].record.snapshot.roster_revision == 9,
            "roster revision should round-trip");
    require(supplements[0].record.snapshot.teams.ct_alive &&
                *supplements[0].record.snapshot.teams.ct_alive == 3,
            "CT alive count should round-trip");

    const auto stream = cspromator::load_replay_stream(session);
    require(stream.size() == 3, "merged v3 replay stream should contain both sources");
    require(stream[0].kind == cspromator::ReplayItemKind::Gsi,
            "first merged replay item should be GSI");
    require(stream[1].kind == cspromator::ReplayItemKind::Supplementary,
            "second merged replay item should be supplementary");
    require(stream[2].kind == cspromator::ReplayItemKind::Gsi,
            "third merged replay item should be GSI");

    const auto metadata = cspromator::load_text_file(session / "metadata.json");
    require(metadata.find("\"schema_version\": 3") != std::string::npos,
            "new sessions should advertise schema version 3");
    require(std::filesystem::exists(session / "raw.gsi"), "v3 session should contain raw.gsi");
    require(std::filesystem::exists(session / "supplementary.timeline.tsv"),
            "v3 session should contain supplementary timeline even when provider is optional");
    require(!std::filesystem::exists(session / "raw"), "v3 session should not allocate per-snapshot raw directory");

    std::filesystem::remove_all(root);
}

void test_v2_packed_replay_compatibility() {
    const auto root = make_temp_root("v2");
    const auto session = root / "session_v2";
    std::filesystem::create_directories(session);

    const std::string first = R"({"packed":1})";
    const std::string second = R"({"packed":2})";
    {
        std::ofstream raw(session / "raw.gsi", std::ios::binary);
        raw << first << second;
    }
    {
        std::ofstream timeline(session / "timeline.tsv", std::ios::binary);
        timeline << "sequence\taccepted_tick\tbody_complete_tick\tack_sent_tick\tpersist_complete_tick\trelative_us\tbody_offset\tbody_bytes\n";
        timeline << "1\t10\t11\t12\t13\t1000\t0\t" << first.size() << "\n";
        timeline << "2\t20\t21\t22\t23\t2000\t" << first.size() << "\t" << second.size() << "\n";
    }

    const auto entries = cspromator::load_timeline(session);
    require(entries.size() == 2, "v2 timeline should still load");
    require(entries[0].packed_v2 && entries[1].packed_v2, "v2 entries should use packed compatibility mode");
    require(entries[0].record.ingress_order == 1 && entries[1].record.ingress_order == 2,
            "v2 replay should synthesize ingress order from GSI sequence");
    require(cspromator::load_replay_body(entries[0]) == first, "first v2 packed body should replay unchanged");
    require(cspromator::load_replay_body(entries[1]) == second, "second v2 packed body should replay unchanged");
    require(cspromator::load_supplementary_timeline(session).empty(),
            "v2 sessions without supplementary file should load an empty supplementary stream");
    require(cspromator::load_replay_stream(session).size() == 2,
            "v2 merged replay should contain its GSI records only");

    std::filesystem::remove_all(root);
}

void test_v1_replay_compatibility() {
    const auto root = make_temp_root("v1");
    const auto session = root / "session_old";
    std::filesystem::create_directories(session / "raw");

    {
        std::ofstream timeline(session / "timeline.tsv", std::ios::binary);
        timeline << "sequence\taccepted_tick\tbody_complete_tick\tack_sent_tick\tpersist_complete_tick\trelative_us\tbody_bytes\tbody_file\n";
        timeline << "1\t10\t11\t12\t13\t1000\t12\t00000001.json\n";
    }
    {
        std::ofstream raw(session / "raw" / "00000001.json", std::ios::binary);
        raw << R"({"old":true})";
    }

    const auto entries = cspromator::load_timeline(session);
    require(entries.size() == 1, "v1 timeline should still load");
    require(!entries[0].packed_v2, "v1 entry should use per-file compatibility mode");
    require(entries[0].record.ingress_order == 1,
            "v1 replay should synthesize ingress order from GSI sequence");
    require(cspromator::load_replay_body(entries[0]) == R"({"old":true})", "v1 raw file should replay unchanged");

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    test_v3_combined_roundtrip();
    test_v2_packed_replay_compatibility();
    test_v1_replay_compatibility();
    std::cout << "CSPromator session storage tests passed.\n";
    return 0;
}
