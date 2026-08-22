#include "cspromator/event_detector.hpp"

#include <sstream>

namespace cspromator {
namespace {

bool equals(const std::optional<std::string>& value, std::string_view expected) {
    return value && *value == expected;
}

bool changed_to(const std::optional<std::string>& previous,
                const std::optional<std::string>& current,
                std::string_view expected) {
    return equals(current, expected) && (!previous || *previous != expected);
}

} // namespace

std::string_view to_string(EventType type) {
    switch (type) {
        case EventType::MatchEntered: return "MATCH_ENTERED";
        case EventType::MatchLeft: return "MATCH_LEFT";
        case EventType::FreezeStarted: return "FREEZE_STARTED";
        case EventType::FreezeEnded: return "FREEZE_ENDED";
        case EventType::RoundStarted: return "ROUND_STARTED";
        case EventType::RoundEnded: return "ROUND_ENDED";
        case EventType::RoundWon: return "ROUND_WON";
        case EventType::RoundLost: return "ROUND_LOST";
        case EventType::TeamChanged: return "TEAM_CHANGED";
        case EventType::LocalPlayerLost: return "LOCAL_PLAYER_LOST";
        case EventType::LocalPlayerRestored: return "LOCAL_PLAYER_RESTORED";
        case EventType::PlayerRespawned: return "PLAYER_RESPAWNED";
        case EventType::PlayerDamaged: return "PLAYER_DAMAGED";
        case EventType::PlayerDied: return "PLAYER_DIED";
        case EventType::PlayerKill: return "PLAYER_KILL";
        case EventType::PlayerHeadshotKill: return "PLAYER_HEADSHOT_KILL";
        case EventType::PlayerAssist: return "PLAYER_ASSIST";
        case EventType::PlayerFlashed: return "PLAYER_FLASHED";
        case EventType::Ace: return "ACE";
        case EventType::MvpGained: return "MVP_GAINED";
        case EventType::BombPlanted: return "BOMB_PLANTED";
        case EventType::BombStateCleared: return "BOMB_STATE_CLEARED";
        case EventType::HalftimeStarted: return "HALFTIME_STARTED";
        case EventType::HalftimeEnded: return "HALFTIME_ENDED";
        case EventType::GameOver: return "GAME_OVER";
    }
    return "UNKNOWN";
}

std::string describe_event(const PromatorEvent& event) {
    std::ostringstream out;
    out << to_string(event.type);
    if (event.amount) {
        out << " amount=" << *event.amount;
    }
    if (event.value) {
        out << " value=" << *event.value;
    }
    if (!event.from.empty() || !event.to.empty()) {
        out << " " << event.from << "->" << event.to;
    }
    return out.str();
}

PromatorEvent EventDetector::make_event(EventType type,
                                        const NormalizedGameState& state) const {
    PromatorEvent event{};
    event.type = type;
    event.sequence = state.sequence;
    event.relative_us = state.relative_us;
    return event;
}

void EventDetector::clear_match_memory() {
    last_ended_round_.reset();
    last_outcome_round_.reset();
    last_known_local_hp_.reset();
    last_known_local_mvps_.reset();
    last_known_local_assists_.reset();
    last_known_local_team_.reset();
    local_observation_lost_ = false;
}

std::vector<PromatorEvent> EventDetector::process(const NormalizedGameState& current) {
    std::vector<PromatorEvent> events;
    if (!current.payload_valid) {
        return events;
    }

    if (!previous_) {
        if (current.map_name) {
            events.push_back(make_event(EventType::MatchEntered, current));
        }
        if (current.local_player_valid) {
            if (current.health) last_known_local_hp_ = current.health;
            if (current.mvps) last_known_local_mvps_ = current.mvps;
            if (current.assists) last_known_local_assists_ = current.assists;
            if (current.player_team) last_known_local_team_ = current.player_team;
        }
        previous_ = current;
        return events;
    }

    const auto& previous = *previous_;

    if (!previous.map_name && current.map_name) {
        clear_match_memory();
        events.push_back(make_event(EventType::MatchEntered, current));
    } else if (previous.map_name && !current.map_name) {
        events.push_back(make_event(EventType::MatchLeft, current));
        clear_match_memory();
        previous_ = current;
        return events;
    }

    const bool current_is_other_observed_player =
        current.provider_steamid && current.observed_steamid && !current.local_player_valid;

    // Observation loss is sticky. Real CS2 can go local -> spectated bot -> no
    // player object -> local between rounds. A pairwise detector loses the trail.
    if (current_is_other_observed_player && !local_observation_lost_) {
        events.push_back(make_event(EventType::LocalPlayerLost, current));
        local_observation_lost_ = true;
    }

    const bool restoring_local_player = local_observation_lost_ && current.local_player_valid;
    if (restoring_local_player) {
        events.push_back(make_event(EventType::LocalPlayerRestored, current));
        if (last_known_local_hp_ && *last_known_local_hp_ == 0 && current.health && *current.health > 0) {
            events.push_back(make_event(EventType::PlayerRespawned, current));
        }
        if (last_known_local_mvps_ && current.mvps && *current.mvps > *last_known_local_mvps_) {
            auto event = make_event(EventType::MvpGained, current);
            event.amount = *current.mvps - *last_known_local_mvps_;
            event.value = *current.mvps;
            events.push_back(std::move(event));
        }
        if (last_known_local_assists_ && current.assists && *current.assists > *last_known_local_assists_) {
            auto event = make_event(EventType::PlayerAssist, current);
            event.amount = *current.assists - *last_known_local_assists_;
            event.value = *current.assists;
            events.push_back(std::move(event));
        }
        local_observation_lost_ = false;
    }

    // Local-player numeric state is comparable only while both snapshots refer
    // to the provider SteamID. Spectator targets must never become fake local
    // healing, negative kills, assists, or MVP changes.
    if (previous.local_player_valid && current.local_player_valid) {
        if (previous.health && current.health && *current.health < *previous.health) {
            auto event = make_event(EventType::PlayerDamaged, current);
            event.amount = *previous.health - *current.health;
            event.value = *current.health;
            events.push_back(std::move(event));
        }
        if (previous.health && current.health && *previous.health > 0 && *current.health == 0) {
            events.push_back(make_event(EventType::PlayerDied, current));
        } else if (previous.health && current.health && *previous.health == 0 && *current.health > 0) {
            events.push_back(make_event(EventType::PlayerRespawned, current));
        }

        if (previous.round_kills && current.round_kills && *current.round_kills > *previous.round_kills) {
            auto event = make_event(EventType::PlayerKill, current);
            event.amount = *current.round_kills - *previous.round_kills;
            event.value = *current.round_kills;
            events.push_back(std::move(event));

            if (*previous.round_kills < 5 && *current.round_kills >= 5 &&
                (equals(current.map_mode, "competitive") || equals(current.map_mode, "casual"))) {
                auto ace = make_event(EventType::Ace, current);
                ace.value = *current.round_kills;
                events.push_back(std::move(ace));
            }
        }

        if (previous.round_headshot_kills && current.round_headshot_kills &&
            *current.round_headshot_kills > *previous.round_headshot_kills) {
            auto event = make_event(EventType::PlayerHeadshotKill, current);
            event.amount = *current.round_headshot_kills - *previous.round_headshot_kills;
            event.value = *current.round_headshot_kills;
            events.push_back(std::move(event));
        }

        if (previous.assists && current.assists && *current.assists > *previous.assists) {
            auto event = make_event(EventType::PlayerAssist, current);
            event.amount = *current.assists - *previous.assists;
            event.value = *current.assists;
            events.push_back(std::move(event));
        }

        if (current.flashed && *current.flashed > 0 &&
            (!previous.flashed || *previous.flashed <= 0)) {
            auto event = make_event(EventType::PlayerFlashed, current);
            event.value = *current.flashed;
            events.push_back(std::move(event));
        }

        if (previous.mvps && current.mvps && *current.mvps > *previous.mvps) {
            auto event = make_event(EventType::MvpGained, current);
            event.amount = *current.mvps - *previous.mvps;
            event.value = *current.mvps;
            events.push_back(std::move(event));
        }
    }

    if (changed_to(previous.bomb_state, current.bomb_state, "planted")) {
        events.push_back(make_event(EventType::BombPlanted, current));
    } else if (equals(previous.bomb_state, "planted") && !equals(current.bomb_state, "planted")) {
        events.push_back(make_event(EventType::BombStateCleared, current));
    }

    const bool game_over_started = changed_to(previous.map_phase, current.map_phase, "gameover");
    const bool halftime_started = changed_to(previous.map_phase, current.map_phase, "intermission");
    const bool halftime_ended = equals(previous.map_phase, "intermission") &&
                                !equals(current.map_phase, "intermission");

    const bool previous_live = equals(previous.round_phase, "live");
    const bool phase_over = previous_live && equals(current.round_phase, "over");
    const bool round_advanced = previous.map_round && current.map_round &&
                                *current.map_round > *previous.map_round;
    const bool winner_appeared = current.round_winner &&
                                 (!previous.round_winner || *previous.round_winner != *current.round_winner);
    const bool round_end_evidence = previous_live &&
                                    (phase_over || round_advanced || winner_appeared || game_over_started);

    std::optional<int> ended_round;
    if (round_end_evidence) {
        ended_round = previous.map_round.value_or(current.map_round.value_or(-1));
        if (!last_ended_round_ || *last_ended_round_ != *ended_round) {
            auto event = make_event(EventType::RoundEnded, current);
            event.value = *ended_round;
            if (current.round_winner) {
                event.to = *current.round_winner;
            }
            events.push_back(std::move(event));
            last_ended_round_ = *ended_round;
        }
    }

    // Determine the result from the last known local team, not the top-level
    // player team, because round end can arrive while we are spectating a bot.
    const auto outcome_team = last_known_local_team_
        ? last_known_local_team_
        : (current.local_player_valid ? current.player_team : std::optional<std::string>{});
    const std::optional<int> outcome_round = ended_round ? ended_round : last_ended_round_;
    if (current.round_winner && outcome_team && outcome_round &&
        (round_end_evidence || winner_appeared) &&
        (!last_outcome_round_ || *last_outcome_round_ != *outcome_round)) {
        auto event = make_event(
            *current.round_winner == *outcome_team ? EventType::RoundWon : EventType::RoundLost,
            current);
        event.value = *outcome_round;
        event.from = *outcome_team;
        event.to = *current.round_winner;
        events.push_back(std::move(event));
        last_outcome_round_ = *outcome_round;
    }

    if (changed_to(previous.round_phase, current.round_phase, "freezetime") &&
        !equals(current.map_phase, "gameover")) {
        events.push_back(make_event(EventType::FreezeStarted, current));
    }

    if (equals(previous.round_phase, "freezetime") && equals(current.round_phase, "live")) {
        events.push_back(make_event(EventType::FreezeEnded, current));
        events.push_back(make_event(EventType::RoundStarted, current));
    }

    if (halftime_started) {
        events.push_back(make_event(EventType::HalftimeStarted, current));
    }
    if (halftime_ended) {
        events.push_back(make_event(EventType::HalftimeEnded, current));
    }

    if (current.local_player_valid && current.player_team && last_known_local_team_ &&
        *current.player_team != *last_known_local_team_) {
        auto event = make_event(EventType::TeamChanged, current);
        event.from = *last_known_local_team_;
        event.to = *current.player_team;
        events.push_back(std::move(event));
    }

    if (game_over_started) {
        events.push_back(make_event(EventType::GameOver, current));
    }

    if (current.local_player_valid) {
        if (current.health) last_known_local_hp_ = current.health;
        if (current.mvps) last_known_local_mvps_ = current.mvps;
        if (current.assists) last_known_local_assists_ = current.assists;
        if (current.player_team) last_known_local_team_ = current.player_team;
    }
    previous_ = current;
    return events;
}

void EventDetector::reset() {
    previous_.reset();
    clear_match_memory();
}

} // namespace cspromator
