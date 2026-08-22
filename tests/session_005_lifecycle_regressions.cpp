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

using cspromator::EventType;
using cspromator::MatchLifecyclePhase;
using cspromator::PromatorEvent;

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

struct Harness {
    cspromator::EventDetector detector;
    cspromator::SemanticResolver resolver;
    std::uint64_t sequence{0};
    std::uint64_t relative_us{0};

    std::vector<PromatorEvent> gsi(std::string_view body) {
        ++sequence;
        relative_us += 1000;
        const auto state = cspromator::normalize_gsi(body, sequence, relative_us);
        require(state.payload_valid, "Session005 synthetic GSI must parse");
        auto events = detector.process(state);
        auto semantic = resolver.process_gsi(state, events);
        events.insert(events.end(), semantic.begin(), semantic.end());
        return events;
    }

    std::vector<PromatorEvent> supplement(int ct_alive,
                                          int t_alive,
                                          int ct_total,
                                          int t_total,
                                          std::uint64_t roster_revision) {
        cspromator::SupplementarySnapshot snapshot;
        snapshot.sequence = ++sequence;
        snapshot.relative_us = ++relative_us;
        snapshot.source = cspromator::SupplementarySourceKind::Synthetic;
        snapshot.roster_revision = roster_revision;
        snapshot.teams.ct_alive = ct_alive;
        snapshot.teams.t_alive = t_alive;
        snapshot.teams.ct_total = ct_total;
        snapshot.teams.t_total = t_total;
        return resolver.process_supplementary(snapshot);
    }
};

void test_session005_multi_match_lifecycle() {
    Harness h;

    // Session005 began in the menu, then briefly attached to a match that was
    // already game-over. That must not invent a round start or local-player loss.
    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "player":{"steamid":"LOCAL","activity":"menu"}
    })");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::Detached,
            "menu should be detached lifecycle");

    const auto gameover_attach = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_dust2","mode":"casual","phase":"gameover","round":9,
             "team_ct":{"score":1},"team_t":{"score":8}},
      "round":{"phase":"freezetime","win_team":"T"}
    })");
    require(has(gameover_attach, EventType::MatchEntered),
            "gameover attachment should still announce match attachment");
    require(!has(gameover_attach, EventType::RoundStarted),
            "attaching at gameover must not invent ROUND_STARTED");
    require(!has(gameover_attach, EventType::LocalPlayerLost),
            "gameover attachment without local player must not invent LOCAL_PLAYER_LOST");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::GameOver,
            "gameover map phase should classify as gameover lifecycle");
    require(!h.resolver.context().local_player_acquired,
            "local player should remain unacquired when no own player object exists");

    const auto first_leave = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "player":{"steamid":"LOCAL","activity":"menu"}
    })");
    require(has(first_leave, EventType::MatchLeft),
            "returning to menu should close the gameover attachment");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::Detached,
            "match leave should clear lifecycle back to detached");

    // The next map began in warmup. Kills, deaths and respawns are factual, but
    // lifecycle context tells the future Director not to treat them as scored play.
    const auto warmup_enter = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"warmup","round":0,
             "team_ct":{"score":0},"team_t":{"score":0}},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":0,"round_killhs":0},
                "match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");
    require(has(warmup_enter, EventType::MatchEntered), "warmup map should attach as a new match");
    require(has(warmup_enter, EventType::LocalPlayerAcquired),
            "first own state in warmup should acquire the local player");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::Warmup,
            "warmup map phase should classify as warmup");
    require(!h.resolver.context().scored_round_active,
            "warmup must not be marked as a scored live round");

    const auto warmup_kill = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"warmup","round":0},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":1,"round_killhs":0},
                "match_stats":{"kills":1,"assists":0,"deaths":0,"mvps":0}}
    })");
    require(has(warmup_kill, EventType::PlayerKill),
            "warmup kill should remain a factual PLAYER_KILL");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::Warmup,
            "warmup kill should retain warmup context");
    require(!has(warmup_kill, EventType::Ace), "warmup kill must not create semantic ACE");
    require(!has(warmup_kill, EventType::ClutchStarted), "warmup kill must not start semantic clutch");

    const auto warmup_death = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"warmup","round":0},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":0,"round_kills":1,"round_killhs":0},
                "match_stats":{"kills":1,"assists":0,"deaths":1,"mvps":0}}
    })");
    require(has(warmup_death, EventType::PlayerDied),
            "warmup death should remain a factual PLAYER_DIED");
    require(h.resolver.context().local_player_alive && !*h.resolver.context().local_player_alive,
            "lifecycle context should remember local death");

    const auto warmup_respawn = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"warmup","round":0},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":2,"round_killhs":1},
                "match_stats":{"kills":2,"assists":0,"deaths":1,"mvps":0}}
    })");
    require(has(warmup_respawn, EventType::PlayerRespawned),
            "warmup respawn should remain factual");

    // Warmup -> real match resets round/match counters downward. Downward resets
    // must not manufacture positive events.
    const auto freeze = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"live","round":0,
             "team_ct":{"score":0},"team_t":{"score":0}},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":0,"round_killhs":0},
                "match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");
    require(!has(freeze, EventType::PlayerKill),
            "warmup counter reset must not create a fake kill");
    require(!has(freeze, EventType::PlayerDied),
            "warmup stat reset must not create a fake death");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::FreezeTime,
            "real match freeze should classify as freezetime");

    const auto live = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"live","round":0},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":0,"round_killhs":0},
                "match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");
    require(has(live, EventType::RoundStarted), "freeze to live should start scored round");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::LiveRound,
            "live round should classify as live-round");
    require(h.resolver.context().scored_round_active,
            "live-round context should mark scored round active");

    // Session005 showed multiple kills and even a death after round.phase=over.
    const auto round_over = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"live","round":1,
             "team_ct":{"score":1},"team_t":{"score":0}},
      "round":{"phase":"over","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":2,"round_killhs":1},
                "match_stats":{"kills":2,"assists":0,"deaths":0,"mvps":0}}
    })");
    require(has(round_over, EventType::RoundEnded), "live to over should end the round once");
    require(has(round_over, EventType::PlayerKill),
            "kills batched into the round-over packet remain factual");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::PostRound,
            "round.phase=over should classify as post-round");
    require(!h.resolver.context().scored_round_active,
            "post-round should not remain scored-round-active context");

    const auto postround_kills = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"live","round":1},
      "round":{"phase":"over","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":4,"round_killhs":2},
                "match_stats":{"kills":4,"assists":0,"deaths":0,"mvps":0}}
    })");
    require(has(postround_kills, EventType::PlayerKill),
            "multiple post-round kills should remain factual PLAYER_KILL");
    require(!has(postround_kills, EventType::RoundEnded),
            "post-round kill packets must not duplicate ROUND_ENDED");
    require(!has(postround_kills, EventType::AceCandidate),
            "post-round kill packets must not manufacture ace candidates");

    const auto postround_death = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"live","round":1},
      "round":{"phase":"over","win_team":"CT"},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":0,"round_kills":4,"round_killhs":2},
                "match_stats":{"kills":4,"assists":0,"deaths":1,"mvps":0}}
    })");
    require(has(postround_death, EventType::PlayerDied),
            "post-round local death should remain a factual PLAYER_DIED");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::PostRound,
            "post-round death must not change lifecycle back to live");

    const auto second_leave = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "player":{"steamid":"LOCAL","activity":"menu"}
    })");
    require(has(second_leave, EventType::MatchLeft),
            "leaving Vertigo should close all match-local memory");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::Detached,
            "leaving Vertigo should detach semantic context");

    // The same recorder process then attached to another Casual map mid-round,
    // initially observing other players. State from Vertigo must not turn this
    // into a restoration/loss cycle.
    const auto fachwerk_attach = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_fachwerk","mode":"casual","phase":"live","round":9,
             "team_ct":{"score":2},"team_t":{"score":7}},
      "round":{"phase":"live"},
      "player":{"steamid":"OTHER_A","team":"T","activity":"playing",
                "state":{"health":87,"round_kills":1,"round_killhs":0},
                "match_stats":{"kills":8,"assists":2,"deaths":4,"mvps":1}}
    })");
    require(has(fachwerk_attach, EventType::MatchEntered),
            "mid-round second map should produce a fresh MATCH_ENTERED");
    require(!has(fachwerk_attach, EventType::LocalPlayerLost),
            "observing another player before local acquisition is not LOCAL_PLAYER_LOST");
    require(!has(fachwerk_attach, EventType::LocalPlayerRestored),
            "new-map spectator state must not inherit restoration state from prior map");
    require(!h.resolver.context().local_player_acquired,
            "second map should begin with local player unacquired");

    const auto other_observed = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_fachwerk","mode":"casual","phase":"live","round":9},
      "round":{"phase":"live"},
      "player":{"steamid":"OTHER_B","team":"CT","activity":"playing",
                "state":{"health":41,"round_kills":2,"round_killhs":1},
                "match_stats":{"kills":6,"assists":1,"deaths":5,"mvps":0}}
    })");
    require(!has(other_observed, EventType::LocalPlayerLost),
            "spectator carousel before first local state should remain silent");

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_fachwerk","mode":"casual","phase":"live","round":10,
             "team_ct":{"score":3},"team_t":{"score":7}},
      "round":{"phase":"over","win_team":"CT"},
      "player":{"steamid":"OTHER_B","team":"CT","activity":"playing",
                "state":{"health":41,"round_kills":2,"round_killhs":1},
                "match_stats":{"kills":6,"assists":1,"deaths":5,"mvps":0}}
    })");

    const auto acquired = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_fachwerk","mode":"casual","phase":"live","round":10,
             "team_ct":{"score":3},"team_t":{"score":7}},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":0,"round_killhs":0},
                "match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");
    require(has(acquired, EventType::LocalPlayerAcquired),
            "first own state on second map should be LOCAL_PLAYER_ACQUIRED");
    require(!has(acquired, EventType::LocalPlayerRestored),
            "first own state on second map must not be LOCAL_PLAYER_RESTORED");
    require(h.resolver.context().local_player_acquired,
            "semantic lifecycle should expose successful local acquisition");
    require(h.resolver.context().lifecycle == MatchLifecyclePhase::FreezeTime,
            "second-map first local state should classify by current freeze phase");
}

void test_match_leave_clears_supplementary_state() {
    Harness h;

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"live","round":1},
      "round":{"phase":"freezetime"},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":0,"round_killhs":0},
                "match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");
    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_vertigo","mode":"casual","phase":"live","round":1},
      "round":{"phase":"live"},
      "player":{"steamid":"LOCAL","team":"CT","activity":"playing",
                "state":{"health":100,"round_kills":0,"round_killhs":0},
                "match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");
    h.supplement(2, 4, 5, 5, 10);
    require(h.resolver.context().supplement_available,
            "first match should accept synthetic supplementary context");

    h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "player":{"steamid":"LOCAL","activity":"menu"}
    })");
    require(!h.resolver.context().supplement_available,
            "MATCH_LEFT must clear supplementary context");

    const auto next_match = h.gsi(R"({
      "provider":{"steamid":"LOCAL"},
      "map":{"name":"de_fachwerk","mode":"casual","phase":"live","round":9},
      "round":{"phase":"live"},
      "player":{"steamid":"OTHER","team":"T","activity":"playing",
                "state":{"health":100,"round_kills":0,"round_killhs":0},
                "match_stats":{"kills":0,"assists":0,"deaths":0,"mvps":0}}
    })");
    require(has(next_match, EventType::MatchEntered), "new map should attach normally");
    require(!h.resolver.context().supplement_available,
            "supplement from previous map must never contaminate next map");
    require(!h.resolver.context().enemy_alive && !h.resolver.context().local_alive,
            "cross-map alive counts must be unknown until a new supplement arrives");
}

} // namespace

int main() {
    test_session005_multi_match_lifecycle();
    test_match_leave_clears_supplementary_state();
    std::cout << "CSPromator Session005 lifecycle regressions passed.\n";
    return 0;
}
