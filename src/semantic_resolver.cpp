#include "cspromator/semantic_resolver.hpp"

#include <algorithm>
#include <string>

namespace cspromator {
namespace {

bool has_event(const std::vector<PromatorEvent>& events, EventType type) {
    return std::any_of(events.begin(), events.end(), [type](const PromatorEvent& event) {
        return event.type == type;
    });
}

bool supported_round_mode_for_ace(const std::optional<std::string>& mode) {
    if (!mode) {
        return false;
    }
    return *mode == "competitive" || *mode == "casual" || *mode == "retakes";
}

} // namespace

SemanticResolver::SemanticResolver(SemanticResolverConfig config)
    : config_(std::move(config)) {}

void SemanticResolver::reset() {
    context_ = {};
    latest_state_.reset();
    latest_supplement_.reset();
    local_team_.reset();
    local_player_alive_.reset();

    round_has_known_start_ = false;
    round_ended_ = false;
    round_start_us_ = 0;
    initial_enemy_total_.reset();
    initial_roster_revision_.reset();
    roster_changed_this_round_ = false;
    ace_emitted_this_round_ = false;

    clutch_active_ = false;
    last_clutch_enemy_alive_.reset();
    last_supplement_us_.reset();
}

bool SemanticResolver::snapshot_fresh_at(
    const SupplementarySnapshot& snapshot,
    std::uint64_t reference_us) const {
    if (snapshot.relative_us > reference_us) {
        return false;
    }
    if (!config_.max_supplement_age_us) {
        return true;
    }
    return reference_us - snapshot.relative_us <= *config_.max_supplement_age_us;
}

void SemanticResolver::begin_round(const NormalizedGameState& state) {
    round_has_known_start_ = true;
    round_ended_ = false;
    round_start_us_ = state.relative_us;
    initial_enemy_total_.reset();
    initial_roster_revision_.reset();
    roster_changed_this_round_ = false;
    ace_emitted_this_round_ = false;
    context_.roster_changed_this_round = false;

    clutch_active_ = false;
    last_clutch_enemy_alive_.reset();
    context_.clutch_active = false;
    context_.clutch_opponents.reset();

    if (latest_supplement_ && snapshot_fresh_at(*latest_supplement_, state.relative_us)) {
        establish_round_baseline(*latest_supplement_);
        update_context(*latest_supplement_);
    }
}

void SemanticResolver::establish_round_baseline(
    const SupplementarySnapshot& snapshot) {
    if (!round_has_known_start_ || !local_team_) {
        return;
    }
    if (config_.require_roster_revision_for_ace && !snapshot.roster_revision) {
        return;
    }

    const auto perspective = perspective_for_team(snapshot.teams, *local_team_);
    if (!perspective || !perspective->enemy_total) {
        return;
    }

    initial_enemy_total_ = perspective->enemy_total;
    initial_roster_revision_ = snapshot.roster_revision;
}

void SemanticResolver::update_context(const SupplementarySnapshot& snapshot) {
    context_.supplement_available = false;
    context_.supplement_source = snapshot.source;
    context_.supplement_relative_us = snapshot.relative_us;
    context_.local_alive.reset();
    context_.enemy_alive.reset();
    context_.local_total.reset();
    context_.enemy_total.reset();
    context_.alive_delta.reset();

    if (!local_team_) {
        return;
    }
    const auto perspective = perspective_for_team(snapshot.teams, *local_team_);
    if (!perspective) {
        return;
    }

    context_.supplement_available = true;
    context_.local_alive = perspective->local_alive;
    context_.enemy_alive = perspective->enemy_alive;
    context_.local_total = perspective->local_total;
    context_.enemy_total = perspective->enemy_total;
    if (perspective->local_alive && perspective->enemy_alive) {
        context_.alive_delta = *perspective->local_alive - *perspective->enemy_alive;
    }
}

void SemanticResolver::observe_roster(const SupplementarySnapshot& snapshot) {
    if (!round_has_known_start_ || !initial_enemy_total_ || !local_team_) {
        return;
    }

    const auto perspective = perspective_for_team(snapshot.teams, *local_team_);
    if (!perspective) {
        return;
    }

    if (perspective->enemy_total && *perspective->enemy_total != *initial_enemy_total_) {
        roster_changed_this_round_ = true;
    }
    if (initial_roster_revision_ && snapshot.roster_revision &&
        *snapshot.roster_revision != *initial_roster_revision_) {
        roster_changed_this_round_ = true;
    }
    context_.roster_changed_this_round = roster_changed_this_round_;
}

PromatorEvent SemanticResolver::make_semantic_event(
    EventType type,
    std::uint64_t event_us) const {
    PromatorEvent event{};
    event.type = type;
    event.evidence = EventEvidence::Deterministic;
    event.sources = EventSource::Gsi | EventSource::SupplementaryState;
    event.relative_us = event_us;
    if (latest_state_) {
        event.sequence = latest_state_->sequence;
    }
    return event;
}

std::optional<PromatorEvent> SemanticResolver::end_clutch(
    std::uint64_t event_us) {
    if (!clutch_active_) {
        return std::nullopt;
    }

    auto event = make_semantic_event(EventType::ClutchEnded, event_us);
    if (last_clutch_enemy_alive_) {
        event.value = *last_clutch_enemy_alive_;
    }

    clutch_active_ = false;
    last_clutch_enemy_alive_.reset();
    context_.clutch_active = false;
    context_.clutch_opponents.reset();
    return event;
}

std::vector<PromatorEvent> SemanticResolver::evaluate_clutch(
    std::uint64_t event_us) {
    std::vector<PromatorEvent> events;
    if (!latest_state_ || !latest_supplement_ || !local_team_) {
        return events;
    }

    const auto perspective = perspective_for_team(latest_supplement_->teams, *local_team_);
    if (!perspective || !perspective->local_alive || !perspective->enemy_alive) {
        if (auto ended = end_clutch(event_us)) {
            events.push_back(std::move(*ended));
        }
        return events;
    }

    const bool live_round = latest_state_->round_phase && *latest_state_->round_phase == "live";
    const bool local_is_alive = local_player_alive_.value_or(false);
    const bool clutch_now = live_round && !round_ended_ && local_is_alive &&
                            *perspective->local_alive == 1 &&
                            *perspective->enemy_alive >= 1;

    if (!clutch_now) {
        if (auto ended = end_clutch(event_us)) {
            events.push_back(std::move(*ended));
        }
        return events;
    }

    if (!clutch_active_) {
        auto event = make_semantic_event(EventType::ClutchStarted, event_us);
        event.value = *perspective->enemy_alive;
        events.push_back(std::move(event));
        clutch_active_ = true;
        last_clutch_enemy_alive_ = perspective->enemy_alive;
    } else if (!last_clutch_enemy_alive_ ||
               *last_clutch_enemy_alive_ != *perspective->enemy_alive) {
        auto event = make_semantic_event(EventType::ClutchUpdated, event_us);
        if (last_clutch_enemy_alive_) {
            event.from = std::to_string(*last_clutch_enemy_alive_);
        }
        event.to = std::to_string(*perspective->enemy_alive);
        event.value = *perspective->enemy_alive;
        events.push_back(std::move(event));
        last_clutch_enemy_alive_ = perspective->enemy_alive;
    }

    context_.clutch_active = clutch_active_;
    context_.clutch_opponents = last_clutch_enemy_alive_;
    return events;
}

std::vector<PromatorEvent> SemanticResolver::evaluate_ace(
    std::uint64_t event_us) {
    std::vector<PromatorEvent> events;
    if (ace_emitted_this_round_ || !round_has_known_start_ ||
        !latest_state_ || !latest_supplement_ || !local_team_ ||
        !initial_enemy_total_ || !supported_round_mode_for_ace(latest_state_->map_mode)) {
        return events;
    }
    if (roster_changed_this_round_) {
        return events;
    }
    if (config_.require_roster_revision_for_ace && !initial_roster_revision_) {
        return events;
    }
    if (!latest_state_->round_kills || *latest_state_->round_kills != *initial_enemy_total_) {
        return events;
    }

    const auto perspective = perspective_for_team(latest_supplement_->teams, *local_team_);
    if (!perspective || !perspective->enemy_alive || !perspective->enemy_total) {
        return events;
    }
    if (*perspective->enemy_alive != 0 || *perspective->enemy_total != *initial_enemy_total_) {
        return events;
    }
    if (initial_roster_revision_) {
        if (!latest_supplement_->roster_revision ||
            *latest_supplement_->roster_revision != *initial_roster_revision_) {
            return events;
        }
    }

    auto event = make_semantic_event(EventType::Ace, event_us);
    event.value = *latest_state_->round_kills;
    events.push_back(std::move(event));
    ace_emitted_this_round_ = true;
    return events;
}

std::vector<PromatorEvent> SemanticResolver::process_gsi(
    const NormalizedGameState& state,
    const std::vector<PromatorEvent>& factual_events) {
    std::vector<PromatorEvent> events;

    if (has_event(factual_events, EventType::MatchEntered)) {
        reset();
    }

    latest_state_ = state;
    if (state.local_player_valid && state.player_team) {
        local_team_ = state.player_team;
    }
    if (state.local_player_valid && state.health) {
        local_player_alive_ = *state.health > 0;
    }
    if (has_event(factual_events, EventType::PlayerDied)) {
        local_player_alive_ = false;
    }
    if (has_event(factual_events, EventType::PlayerRespawned)) {
        local_player_alive_ = true;
    }

    if (has_event(factual_events, EventType::RoundStarted)) {
        begin_round(state);
    }

    if (latest_supplement_ && snapshot_fresh_at(*latest_supplement_, state.relative_us)) {
        update_context(*latest_supplement_);
        observe_roster(*latest_supplement_);

        auto clutch = evaluate_clutch(state.relative_us);
        events.insert(events.end(), clutch.begin(), clutch.end());

        auto ace = evaluate_ace(state.relative_us);
        events.insert(events.end(), ace.begin(), ace.end());
    } else {
        context_.supplement_available = false;
    }

    if (has_event(factual_events, EventType::RoundEnded) ||
        has_event(factual_events, EventType::GameOver) ||
        has_event(factual_events, EventType::MatchLeft)) {
        if (auto ended = end_clutch(state.relative_us)) {
            events.push_back(std::move(*ended));
        }
        round_ended_ = true;
    }

    if (has_event(factual_events, EventType::MatchLeft)) {
        // Keep any semantic end event produced above, but clear knowledge before
        // the next match attaches.
        const auto retained_events = events;
        reset();
        return retained_events;
    }

    return events;
}

std::vector<PromatorEvent> SemanticResolver::process_supplementary(
    const SupplementarySnapshot& snapshot) {
    std::vector<PromatorEvent> events;
    if (!valid_team_counts(snapshot.teams)) {
        return events;
    }
    if (last_supplement_us_ && snapshot.relative_us < *last_supplement_us_) {
        // Cross-source state must never time-travel. A future live adapter should
        // QPC-stamp at observation and submit in monotonic order.
        return events;
    }

    latest_supplement_ = snapshot;
    last_supplement_us_ = snapshot.relative_us;

    if (!latest_state_) {
        return events;
    }

    update_context(snapshot);
    observe_roster(snapshot);

    auto clutch = evaluate_clutch(snapshot.relative_us);
    events.insert(events.end(), clutch.begin(), clutch.end());

    auto ace = evaluate_ace(snapshot.relative_us);
    events.insert(events.end(), ace.begin(), ace.end());
    return events;
}

} // namespace cspromator
