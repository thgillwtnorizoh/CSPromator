#include "cspromator/event_detector.hpp"
#include "cspromator/game_state.hpp"
#include "cspromator/semantic_resolver.hpp"
#include "cspromator/supplementary_state.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

bool has(const std::vector<cspromator::PromatorEvent>& events,
         cspromator::EventType type) {
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

void test_previous_round_alive_state_cannot_confirm_next_round() {
    cspromator::EventDetector detector;
    cspromator::SemanticResolver resolver;

    const auto feed = [&](std::string_view body,
                          std::uint64_t sequence,
                          std::uint64_t relative_us) {
        const auto state = cspromator::normalize_gsi(body, sequence, relative_us);
        auto facts = detector.process(state);
        auto semantic = resolver.process_gsi(state, facts);
        facts.insert(facts.end(), semantic.begin(), semantic.end());
        return facts;
    };

    feed(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 1, 1000);

    cspromator::SupplementarySnapshot previous_round_end;
    previous_round_end.sequence = 1;
    previous_round_end.relative_us = 1500;
    previous_round_end.roster_revision = 5;
    previous_round_end.source = cspromator::SupplementarySourceKind::Synthetic;
    previous_round_end.teams.ct_alive = 1;
    previous_round_end.teams.t_alive = 0;
    previous_round_end.teams.ct_total = 5;
    previous_round_end.teams.t_total = 5;
    resolver.process_supplementary(previous_round_end);

    feed(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 2, 2000);

    const auto fifth = feed(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":5,"round_killhs":0},"match_stats":{"kills":5,"assists":0,"deaths":0,"mvps":0}}
    })", 3, 3000);

    require(has(fifth, cspromator::EventType::AceCandidate),
            "GSI mode rule should still create ACE_CANDIDATE");
    require(!has(fifth, cspromator::EventType::Ace),
            "pre-round enemy_alive=0 must never confirm a current-round ACE");
    require(!has(fifth, cspromator::EventType::ClutchStarted),
            "pre-round alive counts must never manufacture a current-round clutch");

    cspromator::SupplementarySnapshot current_round = previous_round_end;
    current_round.sequence = 2;
    current_round.relative_us = 3100;
    current_round.teams.ct_alive = 1;
    current_round.teams.t_alive = 0;
    const auto confirmed = resolver.process_supplementary(current_round);
    require(has(confirmed, cspromator::EventType::Ace),
            "current-round zero-enemy observation may confirm the ACE");
}

void test_delayed_supplement_cannot_rewind_newer_gsi() {
    cspromator::EventDetector detector;
    cspromator::SemanticResolver resolver;

    const auto state = cspromator::normalize_gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":2},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 10, 10000);
    auto facts = detector.process(state);
    resolver.process_gsi(state, facts);

    cspromator::SupplementarySnapshot delayed;
    delayed.sequence = 1;
    delayed.relative_us = 9000; // observed before GSI, submitted after it
    delayed.roster_revision = 4;
    delayed.source = cspromator::SupplementarySourceKind::Replay;
    delayed.teams.ct_alive = 1;
    delayed.teams.t_alive = 4;
    delayed.teams.ct_total = 5;
    delayed.teams.t_total = 5;

    const auto ignored = resolver.process_supplementary(delayed);
    require(ignored.empty(),
            "supplement older than latest processed GSI should be ignored");
    require(!resolver.context().supplement_available,
            "delayed old supplement must not populate current semantic context");
    require(!resolver.context().enemy_alive,
            "delayed old supplement must not rewind enemy alive count");
}

} // namespace

int main() {
    test_previous_round_alive_state_cannot_confirm_next_round();
    test_delayed_supplement_cannot_rewind_newer_gsi();
    std::cout << "CSPromator stale supplementary regression tests passed.\n";
    return 0;
}
