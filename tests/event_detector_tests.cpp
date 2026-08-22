#include "cspromator/event_detector.hpp"
#include "cspromator/game_state.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using cspromator::EventEvidence;
using cspromator::EventType;
using cspromator::PromatorEvent;

cspromator::NormalizedGameState state(std::string_view body, std::uint64_t seq) {
    return cspromator::normalize_gsi(body, seq, seq * 1000);
}

bool has(const std::vector<PromatorEvent>& events, EventType type) {
    return std::any_of(events.begin(), events.end(), [type](const auto& event) {
        return event.type == type;
    });
}

const PromatorEvent* find(const std::vector<PromatorEvent>& events, EventType type) {
    const auto it = std::find_if(events.begin(), events.end(), [type](const auto& event) {
        return event.type == type;
    });
    return it == events.end() ? nullptr : &(*it);
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void test_death_then_spectator_switch() {
    cspromator::EventDetector detector;

    const auto first = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":1},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":74,"round_kills":3,"round_killhs":3},"match_stats":{"kills":7,"deaths":0,"mvps":1}}
    })", 1));
    require(has(first, EventType::LocalPlayerAcquired), "first valid local state should acquire local player");

    const auto death = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":1},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":0,"round_kills":3,"round_killhs":3},"match_stats":{"kills":7,"deaths":1,"mvps":1}}
    })", 2));
    require(has(death, EventType::PlayerDamaged), "death snapshot should include damage");
    require(has(death, EventType::PlayerDied), "death snapshot should emit PLAYER_DIED");

    const auto spectator = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":1},
      "round":{"phase":"live"},
      "player":{"steamid":"BOT_A","team":"T","state":{"health":100,"round_kills":1,"round_killhs":0},"match_stats":{"kills":2,"deaths":0,"mvps":0}}
    })", 3));
    require(has(spectator, EventType::LocalPlayerLost), "spectator switch should emit LOCAL_PLAYER_LOST");
    require(!has(spectator, EventType::PlayerKill), "spectated bot counters must not create local kills");
    require(!has(spectator, EventType::PlayerDamaged), "spectated bot HP must not create local damage");

    const auto restored = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":2},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":7,"deaths":1,"mvps":2}}
    })", 4));
    require(has(restored, EventType::LocalPlayerRestored), "local player should be restored next round");
    require(has(restored, EventType::PlayerRespawned), "remembered local HP=0 should produce respawn");
    require(has(restored, EventType::MvpGained), "MVP earned while spectating should surface when local stats return");
    require(!has(restored, EventType::PlayerKill), "restoring local counters must not look like a kill jump");
}

void test_ace_candidate_round_end_batch() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":2},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":4,"round_killhs":4},"match_stats":{"kills":11,"deaths":1,"mvps":2}}
    })", 10));

    const auto events = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"intermission","round":3},
      "round":{"phase":"over","win_team":"T"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":5,"round_killhs":5},"match_stats":{"kills":12,"deaths":1,"mvps":3}}
    })", 11));

    require(has(events, EventType::PlayerKill), "fifth kill should be detected in round-end packet");
    require(has(events, EventType::PlayerHeadshotKill), "fifth headshot should be detected in round-end packet");
    require(has(events, EventType::AceCandidate), "5-kill competitive round should emit ACE_CANDIDATE");
    require(!has(events, EventType::Ace), "GSI-only fixed roster rule must not assert confirmed ACE");
    require(has(events, EventType::MvpGained), "MVP increment should be preserved in same batch");
    require(has(events, EventType::BombStateCleared), "planted bomb disappearing at round end should be represented");
    require(has(events, EventType::RoundEnded), "round end should be detected");
    require(has(events, EventType::HalftimeStarted), "intermission transition should emit halftime start");

    const auto* kill = find(events, EventType::PlayerKill);
    require(kill && kill->value && *kill->value == 5, "fifth-kill event should retain current round kill count");
    const auto* ace = find(events, EventType::AceCandidate);
    require(ace && ace->evidence == EventEvidence::ModeAssumption,
            "ACE_CANDIDATE must disclose mode-assumption evidence");
}

void test_halftime_team_swap() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"intermission","round":3},
      "round":{"phase":"over","win_team":"T"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":5,"round_killhs":5},"match_stats":{"kills":12,"deaths":1,"mvps":3}}
    })", 20));

    const auto events = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":3},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":12,"deaths":1,"mvps":3}}
    })", 21));

    require(has(events, EventType::HalftimeEnded), "leaving intermission should emit HALFTIME_ENDED");
    require(has(events, EventType::TeamChanged), "T->CT swap should emit TEAM_CHANGED");
    require(has(events, EventType::FreezeStarted), "post-halftime freeze should emit FREEZE_STARTED");
}

void test_final_round_without_over_phase() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":3},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":74,"round_kills":4,"round_killhs":4},"match_stats":{"kills":16,"deaths":1,"mvps":3}}
    })", 30));

    const auto events = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"gameover","round":4},
      "round":{"phase":"freezetime","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":74,"round_kills":5,"round_killhs":4},"match_stats":{"kills":17,"deaths":1,"mvps":4}}
    })", 31));

    require(has(events, EventType::PlayerKill), "final fifth kill should be detected");
    require(has(events, EventType::AceCandidate), "final five-kill round should emit ACE_CANDIDATE");
    require(!has(events, EventType::Ace), "final packet cannot prove ACE without roster evidence");
    require(has(events, EventType::RoundEnded), "gameover packet must end round without intermediate over phase");
    require(has(events, EventType::GameOver), "map gameover transition should emit GAME_OVER");
    require(!has(events, EventType::FreezeStarted), "gameover freezetime must not masquerade as next-round freeze");
}

} // namespace

int main() {
    test_death_then_spectator_switch();
    test_ace_candidate_round_end_batch();
    test_halftime_team_swap();
    test_final_round_without_over_phase();
    std::cout << "CSPromator event regression tests passed.\n";
    return 0;
}
