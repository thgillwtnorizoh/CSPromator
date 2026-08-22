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

using cspromator::EventSource;
using cspromator::EventType;
using cspromator::PromatorEvent;
using cspromator::SupplementarySnapshot;
using cspromator::SupplementarySourceKind;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

bool has(const std::vector<PromatorEvent>& events, EventType type) {
    return std::any_of(events.begin(), events.end(), [type](const PromatorEvent& event) {
        return event.type == type;
    });
}

const PromatorEvent* find(const std::vector<PromatorEvent>& events, EventType type) {
    const auto it = std::find_if(events.begin(), events.end(), [type](const PromatorEvent& event) {
        return event.type == type;
    });
    return it == events.end() ? nullptr : &*it;
}

struct Harness {
    cspromator::EventDetector detector;
    cspromator::SemanticResolver resolver;

    std::vector<PromatorEvent> gsi(std::string_view body,
                                   std::uint64_t sequence,
                                   std::uint64_t relative_us) {
        const auto state = cspromator::normalize_gsi(body, sequence, relative_us);
        require(state.payload_valid, "synthetic GSI must parse");
        auto events = detector.process(state);
        auto semantic = resolver.process_gsi(state, events);
        events.insert(events.end(), semantic.begin(), semantic.end());
        return events;
    }

    std::vector<PromatorEvent> supplement(std::uint64_t sequence,
                                          std::uint64_t relative_us,
                                          int ct_alive,
                                          int t_alive,
                                          int ct_total,
                                          int t_total,
                                          std::uint64_t roster_revision) {
        SupplementarySnapshot snapshot;
        snapshot.sequence = sequence;
        snapshot.relative_us = relative_us;
        snapshot.roster_revision = roster_revision;
        snapshot.source = SupplementarySourceKind::Synthetic;
        snapshot.teams.ct_alive = ct_alive;
        snapshot.teams.t_alive = t_alive;
        snapshot.teams.ct_total = ct_total;
        snapshot.teams.t_total = t_total;
        return resolver.process_supplementary(snapshot);
    }
};

void test_competitive_ace_promotes_after_late_supplement() {
    Harness h;

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 1, 1000);

    h.supplement(1, 1500, 5, 5, 5, 5, 7);

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 2, 2000);

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":4,"round_killhs":2},"match_stats":{"kills":4,"assists":0,"deaths":0,"mvps":0}}
    })", 3, 3000);

    h.supplement(2, 3100, 5, 1, 5, 5, 7);

    const auto final_kill = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":1},
      "round":{"phase":"over","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":5,"round_killhs":3},"match_stats":{"kills":5,"assists":0,"deaths":0,"mvps":1}}
    })", 4, 4000);

    require(has(final_kill, EventType::AceCandidate), "GSI should still emit the five-kill candidate");
    require(!has(final_kill, EventType::Ace), "stale 1-enemy-alive supplement must not confirm ace early");

    const auto confirmed = h.supplement(3, 4100, 5, 0, 5, 5, 7);
    const auto* ace = find(confirmed, EventType::Ace);
    require(ace != nullptr, "zero enemies with stable roster should promote ACE after GSI kill");
    require(ace->value && *ace->value == 5, "confirmed ACE should retain local round kill count");
    require(cspromator::has_source(ace->sources, EventSource::Gsi), "confirmed ACE should cite GSI");
    require(cspromator::has_source(ace->sources, EventSource::SupplementaryState), "confirmed ACE should cite supplementary state");
}

void test_casual_can_confirm_without_kill_count_candidate() {
    Harness h;

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":9},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 10, 10000);
    h.supplement(10, 10500, 7, 7, 7, 7, 1);
    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":9},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 11, 11000);

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":9},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":6,"round_killhs":2},"match_stats":{"kills":6,"assists":0,"deaths":0,"mvps":0}}
    })", 12, 12000);
    h.supplement(11, 12100, 1, 7, 7, 7, 1);

    const auto seventh = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"casual","phase":"live","round":9},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":7,"round_killhs":2},"match_stats":{"kills":7,"assists":0,"deaths":0,"mvps":0}}
    })", 13, 13000);
    require(!has(seventh, EventType::AceCandidate), "Casual must remain free of fixed kill-count ACE candidates");

    const auto confirmed = h.supplement(12, 13100, 0, 7, 7, 7, 1);
    require(has(confirmed, EventType::Ace), "stable seven-player Casual roster can be confirmed by supplementary evidence");
}

void test_roster_revision_blocks_ace_even_if_total_returns() {
    Harness h;

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":2},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 20, 20000);
    h.supplement(20, 20500, 4, 3, 4, 3, 10);
    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":2},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 21, 21000);

    h.supplement(21, 22000, 5, 3, 5, 3, 11);
    h.supplement(22, 23000, 4, 3, 4, 3, 12);

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"retakes","phase":"live","round":2},
      "round":{"phase":"live","bomb":"planted"},
      "player":{"steamid":"LOCAL","team":"T","state":{"health":100,"round_kills":4,"round_killhs":1},"match_stats":{"kills":4,"assists":0,"deaths":0,"mvps":0}}
    })", 22, 24000);

    const auto final_state = h.supplement(23, 24100, 0, 3, 4, 3, 12);
    require(!has(final_state, EventType::Ace), "join/leave churn must block ACE even when total returns to baseline");
    require(h.resolver.context().roster_changed_this_round, "roster churn should remain sticky for the round");
}

void test_clutch_lifecycle_and_team_balance_context() {
    Harness h;

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":4},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 30, 30000);
    h.supplement(30, 30500, 5, 5, 5, 5, 30);
    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":4},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 31, 31000);

    const auto started = h.supplement(31, 32000, 1, 4, 5, 5, 30);
    const auto* clutch_start = find(started, EventType::ClutchStarted);
    require(clutch_start && clutch_start->value && *clutch_start->value == 4,
            "1v4 should emit CLUTCH_STARTED value=4");

    const auto updated = h.supplement(32, 33000, 1, 2, 5, 5, 30);
    const auto* clutch_update = find(updated, EventType::ClutchUpdated);
    require(clutch_update && clutch_update->value && *clutch_update->value == 2,
            "1v2 should update active clutch");
    require(h.resolver.context().alive_delta && *h.resolver.context().alive_delta == -1,
            "semantic context should expose local-minus-enemy alive delta");

    const auto death = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":4},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":0,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":1,"mvps":0}}
    })", 32, 34000);
    require(has(death, EventType::PlayerDied), "GSI death fact should remain present");
    require(has(death, EventType::ClutchEnded), "local death should terminate the semantic clutch immediately");
}

void test_out_of_order_supplement_is_ignored() {
    Harness h;
    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_test","mode":"competitive","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","state":{"health":100,"round_kills":0,"round_killhs":0},"match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })", 40, 40000);

    h.supplement(40, 42000, 1, 3, 5, 5, 1);
    const auto ignored = h.supplement(41, 41000, 5, 5, 5, 5, 1);
    require(ignored.empty(), "older supplementary snapshots should be ignored rather than time-travel state");
    require(h.resolver.context().supplement_relative_us == 42000,
            "ignored snapshot must not rewind semantic context");
}

} // namespace

int main() {
    test_competitive_ace_promotes_after_late_supplement();
    test_casual_can_confirm_without_kill_count_candidate();
    test_roster_revision_blocks_ace_even_if_total_returns();
    test_clutch_lifecycle_and_team_balance_context();
    test_out_of_order_supplement_is_ignored();
    std::cout << "CSPromator semantic resolver tests passed.\n";
    return 0;
}
