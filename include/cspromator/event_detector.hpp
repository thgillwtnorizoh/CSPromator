#pragma once

#include "cspromator/game_state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cspromator {

enum class EventType {
    MatchEntered,
    MatchLeft,
    FreezeStarted,
    FreezeEnded,
    RoundStarted,
    RoundEnded,
    RoundWon,
    RoundLost,
    TeamChanged,
    LocalPlayerAcquired,
    LocalPlayerLost,
    LocalPlayerRestored,
    PlayerRespawned,
    PlayerDamaged,
    PlayerDied,
    PlayerKill,
    PlayerHeadshotKill,
    PlayerAssist,
    PlayerFlashed,
    PlayerSmoked,
    PlayerBurning,
    AceCandidate,
    Ace,
    ClutchStarted,
    ClutchUpdated,
    ClutchEnded,
    MvpGained,
    BombPlanted,
    BombDefused,
    BombExploded,
    BombStateCleared,
    HalftimeStarted,
    HalftimeEnded,
    GameOver,
};

enum class EventEvidence {
    Deterministic,
    ModeAssumption,
    Heuristic,
};

enum class EventSource : std::uint8_t {
    None = 0,
    Gsi = 1U << 0,
    ModeRules = 1U << 1,
    SupplementaryState = 1U << 2,
};

constexpr EventSource operator|(EventSource lhs, EventSource rhs) {
    return static_cast<EventSource>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr EventSource operator&(EventSource lhs, EventSource rhs) {
    return static_cast<EventSource>(
        static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

constexpr bool has_source(EventSource sources, EventSource source) {
    return (sources & source) != EventSource::None;
}

struct PromatorEvent {
    EventType type{};
    EventEvidence evidence{EventEvidence::Deterministic};
    EventSource sources{EventSource::Gsi};
    std::uint64_t sequence{};
    std::uint64_t relative_us{};
    std::optional<int> amount;
    std::optional<int> value;
    std::string from;
    std::string to;
};

std::string_view to_string(EventType type);
std::string_view to_string(EventEvidence evidence);
std::string describe_sources(EventSource sources);
std::string describe_event(const PromatorEvent& event);

class EventDetector {
public:
    std::vector<PromatorEvent> process(const NormalizedGameState& current);
    void reset();

private:
    PromatorEvent make_event(EventType type, const NormalizedGameState& state) const;
    void clear_match_memory();

    std::optional<NormalizedGameState> previous_;
    std::optional<int> last_ended_round_;
    std::optional<int> last_outcome_round_;
    std::optional<int> last_known_local_hp_;
    std::optional<int> last_known_local_mvps_;
    std::optional<int> last_known_local_assists_;
    std::optional<std::string> last_known_local_team_;
    bool local_player_ever_acquired_{false};
    bool local_observation_lost_{false};
};

} // namespace cspromator
