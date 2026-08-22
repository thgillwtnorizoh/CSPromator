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

void test_spectator_gap_restore() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":5},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":0,"flashed":0,"smoked":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":17,"assists":1,"deaths":2,"mvps":5}}
    })", 1));

    const auto lost = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":5},
      "round":{"phase":"live"},
      "player":{"steamid":"BOT_A","team":"CT","state":{"health":100,"round_kills":1},"match_stats":{"kills":2}}
    })", 2));
    require(has(lost, EventType::LocalPlayerLost), "spectating another player should lose local observation");

    const auto blank = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":5},
      "round":{"phase":"over","win_team":"T"}
    })", 3));
    require(!has(blank, EventType::LocalPlayerRestored), "blank player snapshot must keep observation loss sticky");

    const auto restored = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":6},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"flashed":0,"smoked":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":17,"assists":1,"deaths":2,"mvps":5}}
    })", 4));
    require(has(restored, EventType::LocalPlayerRestored), "local observation must restore across an empty player snapshot");
    require(has(restored, EventType::PlayerRespawned), "remembered local HP zero must produce respawn after restoration");
}

void test_assist_flash_and_smoke_normalization() {
    cspromator::EventDetector detector;

    const auto base = state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":2},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":0,"smoked":0,"round_kills":1,"round_killhs":0},"match_stats":{"kills":3,"assists":0,"deaths":0,"mvps":1}}
    })", 10);
    detector.process(base);

    const auto affected_state = state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":2},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":1,"smoked":74,"round_kills":1,"round_killhs":0},"match_stats":{"kills":3,"assists":1,"deaths":0,"mvps":1}}
    })", 11);
    require(affected_state.flashed && *affected_state.flashed == 1, "flashed state should normalize");
    require(affected_state.smoked && *affected_state.smoked == 74, "smoked state should normalize without inventing a semantic event");

    const auto events = detector.process(affected_state);
    require(has(events, EventType::PlayerAssist), "assist increment should emit PLAYER_ASSIST");
    require(has(events, EventType::PlayerFlashed), "flash rising edge should emit PLAYER_FLASHED");

    const auto still_flashed = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":2},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":1,"smoked":61,"round_kills":1,"round_killhs":0},"match_stats":{"kills":3,"assists":1,"deaths":0,"mvps":1}}
    })", 12));
    require(!has(still_flashed, EventType::PlayerFlashed), "continuous flash state must not spam events");

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":2},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":0,"smoked":0,"round_kills":1,"round_killhs":0},"match_stats":{"kills":3,"assists":1,"deaths":0,"mvps":1}}
    })", 13));

    const auto flashed_again = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":2},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":1,"smoked":0,"round_kills":1,"round_killhs":0},"match_stats":{"kills":3,"assists":1,"deaths":0,"mvps":1}}
    })", 14));
    require(has(flashed_again, EventType::PlayerFlashed), "a later independent flash should emit again after clearing");
}

void test_round_loss_uses_sticky_local_team() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":5},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":17,"assists":1,"deaths":2,"mvps":5}}
    })", 20));

    const auto loss = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":6},
      "round":{"phase":"over","win_team":"T"},
      "player":{"steamid":"BOT_T","team":"T","state":{"health":100},"match_stats":{"kills":4}}
    })", 21));
    require(has(loss, EventType::RoundEnded), "round should end while local player is spectating");
    require(has(loss, EventType::RoundLost), "winner T against remembered local CT must emit ROUND_LOST");
    require(!has(loss, EventType::RoundWon), "spectated bot team must never turn a local loss into a win");
}

void test_round_win() {
    cspromator::EventDetector detector;

    detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":6},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":42,"round_kills":3,"round_killhs":3},"match_stats":{"kills":20,"assists":1,"deaths":2,"mvps":5}}
    })", 30));

    const auto win = detector.process(state(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"gameover","round":7},
      "round":{"phase":"freezetime","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":0,"round_kills":4,"round_killhs":4},"match_stats":{"kills":21,"assists":1,"deaths":3,"mvps":6}}
    })", 31));
    require(has(win, EventType::RoundEnded), "final gameover packet should end round");
    require(has(win, EventType::RoundWon), "winner matching local CT should emit ROUND_WON");
    require(has(win, EventType::GameOver), "final packet should still emit GAME_OVER");
}

} // namespace

int main() {
    test_spectator_gap_restore();
    test_assist_flash_and_smoke_normalization();
    test_round_loss_uses_sticky_local_team();
    test_round_win();
    std::cout << "CSPromator session 002 regression tests passed.\n";
    return 0;
}
