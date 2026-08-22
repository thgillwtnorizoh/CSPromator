#include "cspromator/live_event_pipeline.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    std::mutex mutex;
    std::vector<std::string> seen;

    cspromator::LiveEventPipeline pipeline(
        [&](const cspromator::NormalizedGameState&,
            const std::vector<cspromator::PromatorEvent>& events) {
            std::lock_guard lock(mutex);
            for (const auto& event : events) {
                seen.emplace_back(cspromator::to_string(event.type));
            }
        });

    pipeline.enqueue(1, 1000, R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":0,"smoked":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");

    pipeline.enqueue(2, 2000, R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":0,"smoked":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");

    pipeline.enqueue(3, 3000, R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"flashed":1,"smoked":0,"round_kills":1,"round_killhs":1},"match_stats":{"kills":1,"assists":0,"deaths":0,"mvps":0}}
    })");

    pipeline.stop_and_flush();

    const auto contains = [&](const char* name) {
        return std::find(seen.begin(), seen.end(), name) != seen.end();
    };

    require(contains("MATCH_ENTERED"), "first live payload should enter the match");
    require(contains("FREEZE_ENDED"), "second live payload should end freeze time");
    require(contains("ROUND_STARTED"), "second live payload should start the round");
    require(contains("PLAYER_KILL"), "third live payload should emit a kill");
    require(contains("PLAYER_HEADSHOT_KILL"), "third live payload should emit a headshot kill");
    require(contains("PLAYER_FLASHED"), "third live payload should emit flash rising edge");

    std::cout << "CSPromator live event pipeline tests passed.\n";
    return 0;
}
