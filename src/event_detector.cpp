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
        case EventType::TeamChanged: return "TEAM_CHANGED";
        case EventType::LocalPlayerLost: return "LOCAL_PLAYER_LOST";
        case EventType::LocalPlayerRestored: return "LOCAL_PLAYER_RESTORED";
        case EventType::PlayerRespawned: return "PLAYER_RESPAWNED";
        case EventType::PlayerDamaged: return "PLAYER_DAMAGED";
        case EventType::PlayerDied: return "PLAYER_DIED";
        case EventType::PlayerKill: return "PLAYER_KILL";
        case EventType::PlayerHeadshotKill: return "PLAYER_HEADSHOT_KILL";
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
        }
        previous_ = current;
        return events;
    }

    const auto& previous = *previous_;

    if (!previous.map_name && current.map_name) {
        events.push_back(make_event(EventType::MatchEntered, current));
    } else if (previous.map_name && !current.map_name) {
        events.push_back(make_event(EventType::MatchLeft, current));
    }

    const bool current_is_other_observed_player =
        current.provider_steamid && current.observed_steamid && !current.local_player_valid;
    const bool previous_was_other_observed_player =
        previous.provider_steamid && previous.observed_steamid && !previous.local_player_valid;

    if (previous.local_player_valid && current_is_other_observed_player) {
        events.push_back(make_event(EventType::LocalPlayerLost, current));
    } else if (previous_was_other_observed_player && current.local_player_valid) {
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
    }

    // Local-player numeric state is comparable only while both snapshots refer
    // to the provider SteamID. This prevents spectator targets from looking
    // like impossible negative kills, healing, or MVP loss.
    if (previous.local_player_valid && current.local_player_valid) {
        if (previous.health && current.health && *current.health < *previous.health) {
            auto event = make_event(EventType::PlayerDamaged, current);
            event.amount = *previous.health - *current.health;
            event.value = *current.health;
            events.push_back(std::move(event));
        }
        if (previous.health && current.health && *previous.health > 0 && *current.health == 0) {
            events.push_back(make_event(EventType::PlayerDied, current));
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

    if (round_end_evidence) {
        const int ended_round = previous.map_round.value_or(current.map_round.value_or(-1));
        if (!last_ended_round_ || *last_ended_round_ != ended_round) {
            auto event = make_event(EventType::RoundEnded, current);
            event.value = ended_round;
            if (current.round_winner) {
                event.to = *current.round_winner;
            }
            events.push_back(std::move(event));
            last_ended_round_ = ended_round;
        }
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

    if (previous.local_player_valid && current.local_player_valid &&
        previous.player_team && current.player_team && *previous.player_team != *current.player_team) {
        auto event = make_event(EventType::TeamChanged, current);
        event.from = *previous.player_team;
        event.to = *current.player_team;
        events.push_back(std::move(event));
    }

    if (game_over_started) {
        events.push_back(make_event(EventType::GameOver, current));
    }

    if (current.local_player_valid) {
        if (current.health) last_known_local_hp_ = current.health;
        if (current.mvps) last_known_local_mvps_ = current.mvps;
    }
    previous_ = current;
    return events;
}

void EventDetector::reset() {
    previous_.reset();
    last_ended_round_.reset();
    last_known_local_hp_.reset();
    last_known_local_mvps_.reset();
}

} // namespace cspromator
