#include "cspromator/clock.hpp"
#include "cspromator/event_detector.hpp"
#include "cspromator/game_state.hpp"
#include "cspromator/http_server.hpp"
#include "cspromator/live_event_pipeline.hpp"
#include "cspromator/semantic_resolver.hpp"
#include "cspromator/session.hpp"
#include "cspromator/supplementary_state.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

cspromator::GsiHttpServer* g_active_server = nullptr;

void handle_signal(int) {
    if (g_active_server) {
        g_active_server->request_stop();
    }
}

void print_usage() {
    std::cout
        << "CSPromator Probe 0.0.7\n\n"
        << "Usage:\n"
        << "  cspromator-probe record [port] [sessions-dir]\n"
        << "  cspromator-probe replay <session-dir> [speed] [dump]\n"
        << "  cspromator-probe analyze <session-dir>\n"
        << "  cspromator-probe clock\n\n"
        << "Examples:\n"
        << "  cspromator-probe record 3010 sessions\n"
        << "  cspromator-probe replay sessions/session_20260822_110000 10\n"
        << "  cspromator-probe analyze sessions/session_20260822_110000\n";
}

std::string optional_count(const std::optional<int>& value) {
    return value ? std::to_string(*value) : "?";
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
    cspromator::LiveEventPipeline live_events(
        [](const cspromator::NormalizedGameState& state,
           const std::vector<cspromator::PromatorEvent>& events) {
            std::ostringstream out;
            out << "[LIVE] #" << state.sequence
                << " t+" << (state.relative_us / 1000.0) << " ms\n";
            for (const auto& event : events) {
                out << "  " << cspromator::describe_event(event) << "\n";
            }
            std::cout << out.str() << std::flush;
        });
    cspromator::GsiHttpServer server(port, clock, recorder, live_events);

    g_active_server = &server;
    const auto old_sigint = std::signal(SIGINT, handle_signal);
#ifdef SIGTERM
    const auto old_sigterm = std::signal(SIGTERM, handle_signal);
#endif
    const int result = server.run();
    g_active_server = nullptr;
    std::signal(SIGINT, old_sigint);
#ifdef SIGTERM
    std::signal(SIGTERM, old_sigterm);
#endif

    live_events.stop_and_flush();
    recorder.stop_and_flush();
    std::cout << "[PROMATOR] Live events and session flushed.\n";
    return result;
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

    const auto stream = cspromator::load_replay_stream(session);
    if (stream.empty()) {
        std::cout << "[REPLAY] No recorded observations.\n";
        return 0;
    }

    std::size_t gsi_count = 0;
    std::size_t supplementary_count = 0;
    for (const auto& item : stream) {
        if (item.kind == cspromator::ReplayItemKind::Gsi) ++gsi_count;
        else ++supplementary_count;
    }

    std::cout << "[REPLAY] observations=" << stream.size()
              << " gsi=" << gsi_count
              << " supplementary=" << supplementary_count
              << " speed=" << speed << "x\n";

    std::uint64_t previous_us = stream.front().relative_us;
    for (std::size_t i = 0; i < stream.size(); ++i) {
        const auto& item = stream[i];
        if (i > 0) {
            const auto delta_us = item.relative_us - previous_us;
            const auto scaled = static_cast<std::uint64_t>(static_cast<double>(delta_us) / speed);
            std::this_thread::sleep_for(std::chrono::microseconds(scaled));
        }
        previous_us = item.relative_us;

        if (item.kind == cspromator::ReplayItemKind::Gsi) {
            const auto& entry = *item.gsi;
            std::cout << "[REPLAY GSI] #" << entry.record.sequence
                      << " order=" << entry.record.ingress_order
                      << " t+" << (entry.record.relative_us / 1000.0) << " ms"
                      << " bytes=" << entry.record.body_bytes << "\n";
            if (dump) {
                std::cout << cspromator::load_replay_body(entry) << "\n";
            }
            continue;
        }

        const auto& entry = *item.supplementary;
        const auto& snapshot = entry.record.snapshot;
        std::cout << "[REPLAY SUPPLEMENT] #" << snapshot.sequence
                  << " order=" << entry.record.ingress_order
                  << " t+" << (snapshot.relative_us / 1000.0) << " ms"
                  << " recorded-source=" << cspromator::to_string(entry.recorded_source)
                  << " CT=" << optional_count(snapshot.teams.ct_alive)
                  << "/" << optional_count(snapshot.teams.ct_total)
                  << " T=" << optional_count(snapshot.teams.t_alive)
                  << "/" << optional_count(snapshot.teams.t_total);
        if (snapshot.roster_revision) {
            std::cout << " roster-revision=" << *snapshot.roster_revision;
        }
        std::cout << "\n";
    }
    return 0;
}

int command_analyze(int argc, char** argv) {
    if (argc < 3) {
        throw std::runtime_error("analyze requires <session-dir>");
    }
    const std::filesystem::path session = argv[2];
    const auto stream = cspromator::load_replay_stream(session);
    cspromator::EventDetector detector;
    cspromator::SemanticResolver resolver;
    std::map<cspromator::EventType, std::size_t> counts;
    std::size_t invalid_payloads = 0;
    std::size_t gsi_snapshots = 0;
    std::size_t supplementary_snapshots = 0;

    for (const auto& item : stream) {
        std::vector<cspromator::PromatorEvent> events;

        if (item.kind == cspromator::ReplayItemKind::Gsi) {
            ++gsi_snapshots;
            const auto& entry = *item.gsi;
            const auto body = cspromator::load_replay_body(entry);
            const auto state = cspromator::normalize_gsi(
                body, entry.record.sequence, entry.record.relative_us);
            if (!state.payload_valid) {
                ++invalid_payloads;
                continue;
            }

            events = detector.process(state);
            auto semantic = resolver.process_gsi(state, events);
            events.insert(events.end(), semantic.begin(), semantic.end());

            if (!events.empty()) {
                std::cout << "[BATCH GSI] #" << state.sequence
                          << " order=" << entry.record.ingress_order
                          << " t+" << (state.relative_us / 1000.0) << " ms"
                          << " lifecycle=" << cspromator::to_string(resolver.context().lifecycle)
                          << "\n";
            }
        } else {
            ++supplementary_snapshots;
            const auto& entry = *item.supplementary;
            events = resolver.process_supplementary(entry.record.snapshot);
            if (!events.empty()) {
                std::cout << "[BATCH SUPPLEMENT] #" << entry.record.snapshot.sequence
                          << " order=" << entry.record.ingress_order
                          << " t+" << (entry.record.snapshot.relative_us / 1000.0) << " ms"
                          << " recorded-source=" << cspromator::to_string(entry.recorded_source)
                          << "\n";
            }
        }

        for (const auto& event : events) {
            ++counts[event.type];
            std::cout << "  " << cspromator::describe_event(event) << "\n";
        }
    }

    std::cout << "\n[ANALYZE] observations=" << stream.size()
              << " gsi=" << gsi_snapshots
              << " supplementary=" << supplementary_snapshots
              << " invalid=" << invalid_payloads << "\n";
    for (const auto& [type, count] : counts) {
        std::cout << "  " << cspromator::to_string(type) << "=" << count << "\n";
    }
    return invalid_payloads == 0 ? 0 : 3;
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
        if (command == "analyze") {
            return command_analyze(argc, argv);
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
