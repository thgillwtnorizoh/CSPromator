#include "cspromator/clock.hpp"
#include "cspromator/http_server.hpp"
#include "cspromator/session.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void print_usage() {
    std::cout
        << "CSPromator Probe 0.0.1\n\n"
        << "Usage:\n"
        << "  cspromator-probe record [port] [sessions-dir]\n"
        << "  cspromator-probe replay <session-dir> [speed] [dump]\n"
        << "  cspromator-probe clock\n\n"
        << "Examples:\n"
        << "  cspromator-probe record 3010 sessions\n"
        << "  cspromator-probe replay sessions/session_20260822_110000 1.0\n"
        << "  cspromator-probe replay sessions/session_20260822_110000 10 dump\n";
}

int command_clock() {
    cspromator::MonotonicClock clock;
    const auto info = clock.info();
    std::cout << "Clock: " << info.name << "\n"
              << "Frequency: " << info.frequency << " ticks/sec\n"
              << "Current tick: " << clock.now() << "\n";
    return 0;
}

int command_record(int argc, char** argv) {
    std::uint16_t port = 3010;
    std::filesystem::path sessions = "sessions";
    if (argc >= 3) {
        const int parsed = std::stoi(argv[2]);
        if (parsed < 1 || parsed > 65535) {
            throw std::runtime_error("Port must be 1..65535");
        }
        port = static_cast<std::uint16_t>(parsed);
    }
    if (argc >= 4) {
        sessions = argv[3];
    }

    cspromator::MonotonicClock clock;
    cspromator::SessionRecorder recorder(sessions, clock);
    cspromator::GsiHttpServer server(port, clock, recorder);
    return server.run();
}

int command_replay(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error("replay requires <session-dir>");
    }
    const std::filesystem::path session = argv[2];
    double speed = 1.0;
    if (argc >= 4) {
        speed = std::stod(argv[3]);
        if (speed <= 0.0) {
            throw std::runtime_error("Replay speed must be > 0");
        }
    }
    const bool dump = argc >= 5 && std::string(argv[4]) == "dump";

    const auto entries = cspromator::load_timeline(session);
    if (entries.empty()) {
        std::cout << "[REPLAY] No snapshots.\n";
        return 0;
    }

    std::cout << "[REPLAY] " << entries.size() << " snapshots at " << speed << "x\n";
    std::uint64_t previous_us = entries.front().record.relative_us;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (i > 0) {
            const auto delta_us = entry.record.relative_us - previous_us;
            const auto scaled = static_cast<std::uint64_t>(static_cast<double>(delta_us) / speed);
            std::this_thread::sleep_for(std::chrono::microseconds(scaled));
        }
        previous_us = entry.record.relative_us;

        std::cout << "[REPLAY] #" << entry.record.sequence
                  << " t+" << (entry.record.relative_us / 1000.0) << " ms"
                  << " bytes=" << entry.record.body_bytes
                  << " file=" << entry.record.body_filename << "\n";
        if (dump) {
            std::cout << cspromator::load_text_file(entry.body_path) << "\n";
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 0;
        }
        const std::string command = argv[1];
        if (command == "record") {
            return command_record(argc, argv);
        }
        if (command == "replay") {
            return command_replay(argc, argv);
        }
        if (command == "clock") {
            return command_clock();
        }
        print_usage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "[PROMATOR ERROR] " << ex.what() << "\n";
        return 2;
    }
}
