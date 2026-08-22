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

void test_retakes_bomb_defuse_lifecycle() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 1));

    const auto planted = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":0},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 2));
    require(has(planted, EventType::BombPlanted), "retakes post-plant state should emit BOMB_PLANTED");

    const auto defused = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":1},
      "round":{"phase":"over","bomb":"defused","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":1,"round_killhs":1},"match_stats":{"kills":1,"assists":0,"deaths":0,"mvps":1}}
    })", 3));
    require(has(defused, EventType::BombDefused), "planted->defused should emit BOMB_DEFUSED");
    require(has(defused, EventType::BombStateCleared), "planted->defused should retain generic clear event");
    require(has(defused, EventType::RoundEnded), "defuse winner packet should end the round");
    require(has(defused, EventType::RoundWon), "remembered CT team should produce ROUND_WON");
}

void test_smoke_burning_rising_edges() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":2},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":2,"assists":0,"deaths":0,"mvps":1}}
    })", 10));

    const auto entered = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":2},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":92,"flashed":0,"smoked":1,"burning":255,"round_kills":0,"round_killhs":0},"match_stats":{"kills":2,"assists":0,"deaths":0,"mvps":1}}
    })", 11));
    require(has(entered, EventType::PlayerSmoked), "smoke 0->positive should emit PLAYER_SMOKED");
    require(has(entered, EventType::PlayerBurning), "burning 0->positive should emit PLAYER_BURNING");

    const auto decay = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":2},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":88,"flashed":0,"smoked":131,"burning":230,"round_kills":0,"round_killhs":0},"match_stats":{"kills":2,"assists":0,"deaths":0,"mvps":1}}
    })", 12));
    require(!has(decay, EventType::PlayerSmoked), "positive smoke decay must not spam PLAYER_SMOKED");
    require(!has(decay, EventType::PlayerBurning), "positive burning decay must not spam PLAYER_BURNING");
}

void test_retakes_ct_triple_kill_is_candidate_not_confirmed_ace() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":3},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":2,"round_killhs":2},"match_stats":{"kills":5,"assists":0,"deaths":0,"mvps":2}}
    })", 20));

    const auto third = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":3},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":3,"round_killhs":3},"match_stats":{"kills":6,"assists":0,"deaths":0,"mvps":2}}
    })", 21));
    require(has(third, EventType::PlayerKill), "third retakes kill should remain a normal kill event");
    require(has(third, EventType::AceCandidate), "CT three-kill retakes threshold should emit ACE_CANDIDATE");
    require(!has(third, EventType::Ace), "retakes roster churn prevents GSI-only confirmed ACE");
    const auto* candidate = find(third, EventType::AceCandidate);
    require(candidate && candidate->evidence == EventEvidence::ModeAssumption,
            "retakes ACE_CANDIDATE should disclose mode-assumption evidence");
}

void test_retakes_t_four_kill_candidate() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":4},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":3,"round_killhs":1},"match_stats":{"kills":9,"assists":0,"deaths":0,"mvps":2}}
    })", 25));

    const auto fourth = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":4},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":4,"round_killhs":1},"match_stats":{"kills":10,"assists":0,"deaths":0,"mvps":2}}
    })", 26));
    require(has(fourth, EventType::AceCandidate), "T four-kill retakes threshold should emit ACE_CANDIDATE");
    require(!has(fourth, EventType::Ace), "T threshold still cannot prove the live roster from GSI alone");
}

void test_post_round_kill_does_not_end_round_or_create_ace_candidate() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":4},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":2,"round_killhs":1},"match_stats":{"kills":7,"assists":0,"deaths":0,"mvps":2}}
    })", 30));

    const auto ended = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":5},
      "round":{"phase":"over","bomb":"defused","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":2,"round_killhs":1},"match_stats":{"kills":7,"assists":0,"deaths":0,"mvps":3}}
    })", 31));
    require(has(ended, EventType::RoundEnded), "winner packet should end round once");

    const auto late_kill = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":5},
      "round":{"phase":"over","bomb":"defused","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"burning":0,"round_kills":3,"round_killhs":2},"match_stats":{"kills":8,"assists":0,"deaths":0,"mvps":3}}
    })", 32));
    require(has(late_kill, EventType::PlayerKill), "post-round kill telemetry should remain observable");
    require(!has(late_kill, EventType::RoundEnded), "post-round kill must not duplicate ROUND_ENDED");
    require(!has(late_kill, EventType::AceCandidate), "post-round threshold crossing must not fabricate ACE_CANDIDATE");
}

} // namespace

int main() {
    test_retakes_bomb_defuse_lifecycle();
    test_smoke_burning_rising_edges();
    test_retakes_ct_triple_kill_is_candidate_not_confirmed_ace();
    test_retakes_t_four_kill_candidate();
    test_post_round_kill_does_not_end_round_or_create_ace_candidate();
    std::cout << "CSPromator Session 003 Retakes regressions passed.\n";
    return 0;
}
