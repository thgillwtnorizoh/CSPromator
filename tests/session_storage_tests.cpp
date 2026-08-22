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

void test_v2_packed_roundtrip() {
    const auto root = make_temp_root("v2");
    std::filesystem::create_directories(root);

    cspromator::MonotonicClock clock;
    std::filesystem::path session;
    {
        cspromator::SessionRecorder recorder(root, clock);
        session = recorder.directory();
        const auto a = clock.now();
        recorder.enqueue(a, a, a, R"({"n":1})");
        const auto b = clock.now();
        recorder.enqueue(b, b, b, R"({"n":2,"text":"tomato"})");
        recorder.stop_and_flush();
    }

    const auto entries = cspromator::load_timeline(session);
    require(entries.size() == 2, "v2 timeline should contain two records");
    require(entries[0].packed_v2 && entries[1].packed_v2, "v2 entries should use packed storage");
    require(cspromator::load_replay_body(entries[0]) == R"({"n":1})", "first packed body should round-trip exactly");
    require(cspromator::load_replay_body(entries[1]) == R"({"n":2,"text":"tomato"})", "second packed body should round-trip exactly");
    require(std::filesystem::exists(session / "raw.gsi"), "v2 session should contain raw.gsi");
    require(!std::filesystem::exists(session / "raw"), "v2 session should not allocate per-snapshot raw directory");

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
    require(cspromator::load_replay_body(entries[0]) == R"({"old":true})", "v1 raw file should replay unchanged");

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    test_v2_packed_roundtrip();
    test_v1_replay_compatibility();
    std::cout << "CSPromator session storage tests passed.\n";
    return 0;
}
