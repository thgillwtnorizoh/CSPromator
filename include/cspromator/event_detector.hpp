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

struct PromatorEvent {
    EventType type{};
    EventEvidence evidence{EventEvidence::Deterministic};
    std::uint64_t sequence{};
    std::uint64_t relative_us{};
    std::optional<int> amount;
    std::optional<int> value;
    std::string from;
    std::string to;
};

std::string_view to_string(EventType type);
std::string_view to_string(EventEvidence evidence);
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
