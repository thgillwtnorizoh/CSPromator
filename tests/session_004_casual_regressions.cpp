#include "cspromator/event_detector.hpp"
#include "cspromator/game_state.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

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

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void test_mid_match_join_acquires_before_loss_restore_semantics() {
    cspromator::EventDetector detector;

    const auto attached = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":8,"team_ct":{"score":5},"team_t":{"score":3}},
      "round":{"phase":"live"}
    })", 1));
    require(has(attached, EventType::MatchEntered), "mid-match attachment should emit MATCH_ENTERED");
    require(!has(attached, EventType::LocalPlayerAcquired), "no local state means local player is not acquired yet");

    const auto spectating = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":8,"team_ct":{"score":5},"team_t":{"score":3}},
      "round":{"phase":"live"},
      "player":{"steamid":"OTHER_A","team":"CT","state":{"health":72,"round_kills":2},"match_stats":{"kills":9,"deaths":4}}
    })", 2));
    require(!has(spectating, EventType::LocalPlayerLost), "spectating before first local state is not LOCAL_PLAYER_LOST");

    const auto acquired = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":9,"team_ct":{"score":6},"team_t":{"score":3}},
      "round":{"phase":"freezetime","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 3));
    require(has(acquired, EventType::LocalPlayerAcquired), "first real local state should emit LOCAL_PLAYER_ACQUIRED");
    require(!has(acquired, EventType::LocalPlayerRestored), "first acquisition is not restoration");
    require(!has(acquired, EventType::PlayerRespawned), "joining next round is not a respawn event");

    const auto died = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":9},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":0,"round_kills":1,"round_killhs":0},"match_stats":{"kills":1,"assists":0,"deaths":1,"mvps":0}}
    })", 4));
    require(has(died, EventType::PlayerDied), "actual local death should still be detected");

    const auto lost = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":9},
      "round":{"phase":"live"},
      "player":{"steamid":"OTHER_B","team":"CT","state":{"health":100,"round_kills":0},"match_stats":{"kills":5,"deaths":3}}
    })", 5));
    require(has(lost, EventType::LocalPlayerLost), "spectating after acquisition should emit LOCAL_PLAYER_LOST");

    const auto restored = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":10},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":1,"assists":0,"deaths":1,"mvps":0}}
    })", 6));
    require(has(restored, EventType::LocalPlayerRestored), "local state after real loss should restore");
    require(has(restored, EventType::PlayerRespawned), "HP 0 before observation loss should restore as respawn");
    require(!has(restored, EventType::LocalPlayerAcquired), "restoration must not reacquire the same local identity");
}

void test_casual_five_kills_do_not_assert_ace() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":10},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":4,"round_killhs":2},"match_stats":{"kills":12,"assists":0,"deaths":1,"mvps":0}}
    })", 10));

    const auto fifth = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":10},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":5,"round_killhs":2},"match_stats":{"kills":13,"assists":0,"deaths":1,"mvps":0}}
    })", 11));
    require(has(fifth, EventType::PlayerKill), "Casual fifth kill remains factual PLAYER_KILL telemetry");
    require(!has(fifth, EventType::AceCandidate), "fluid Casual roster must not create ACE_CANDIDATE from five kills");
    require(!has(fifth, EventType::Ace), "Casual GSI cannot prove ACE");
}

void test_bomb_explosion_is_explicit_and_generic_clear_remains() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":11},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":31,"round_kills":1,"round_killhs":1},"match_stats":{"kills":14,"assists":0,"deaths":1,"mvps":0}}
    })", 20));

    const auto exploded = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":12},
      "round":{"phase":"over","bomb":"exploded","win_team":"T"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":31,"round_kills":1,"round_killhs":1},"match_stats":{"kills":14,"assists":0,"deaths":1,"mvps":0}}
    })", 21));
    require(has(exploded, EventType::BombExploded), "planted->exploded should emit BOMB_EXPLODED");
    require(has(exploded, EventType::BombStateCleared), "explosion should retain generic planted-state clear event");
    require(has(exploded, EventType::RoundEnded), "explosion winner packet should end the round");
    require(has(exploded, EventType::RoundLost), "remembered CT team should lose a T explosion round");
}

} // namespace

int main() {
    test_mid_match_join_acquires_before_loss_restore_semantics();
    test_casual_five_kills_do_not_assert_ace();
    test_bomb_explosion_is_explicit_and_generic_clear_remains();
    std::cout << "CSPromator Session 004 Casual regressions passed.\n";
    return 0;
}
