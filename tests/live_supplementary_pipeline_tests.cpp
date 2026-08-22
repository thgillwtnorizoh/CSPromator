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
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");

    cspromator::SupplementarySnapshot baseline;
    baseline.sequence = 1;
    baseline.relative_us = 1500;
    baseline.roster_revision = 1;
    baseline.source = cspromator::SupplementarySourceKind::Synthetic;
    baseline.teams.ct_alive = 5;
    baseline.teams.t_alive = 5;
    baseline.teams.ct_total = 5;
    baseline.teams.t_total = 5;
    pipeline.enqueue_supplementary(baseline);

    pipeline.enqueue(2, 2000, R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");

    cspromator::SupplementarySnapshot clutch = baseline;
    clutch.sequence = 2;
    clutch.relative_us = 2500;
    clutch.teams.ct_alive = 1;
    clutch.teams.t_alive = 2;
    pipeline.enqueue_supplementary(clutch);

    pipeline.stop_and_flush();

    const auto contains = [&](const char* name) {
        return std::find(seen.begin(), seen.end(), name) != seen.end();
    };

    require(contains("MATCH_ENTERED"), "GSI path should still emit MATCH_ENTERED");
    require(contains("ROUND_STARTED"), "GSI path should still emit ROUND_STARTED");
    require(contains("CLUTCH_STARTED"), "supplementary push should emit CLUTCH_STARTED without another GSI packet");

    std::cout << "CSPromator live supplementary pipeline tests passed.\n";
    return 0;
}
