#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cspromator {

struct NormalizedGameState {
    std::uint64_t sequence{};
    std::uint64_t relative_us{};
    bool payload_valid{false};

    std::optional<std::string> provider_steamid;
    std::optional<std::int64_t> provider_timestamp;

    std::optional<std::string> map_name;
    std::optional<std::string> map_mode;
    std::optional<std::string> map_phase;
    std::optional<int> map_round;
    std::optional<int> ct_score;
    std::optional<int> t_score;

    std::optional<std::string> round_phase;
    std::optional<std::string> round_winner;
    std::optional<std::string> bomb_state;

    std::optional<std::string> observed_steamid;
    bool local_player_valid{false};
    std::optional<std::string> player_team;
    std::optional<std::string> player_activity;
    std::optional<int> health;
    std::optional<int> armor;
    std::optional<int> money;
    std::optional<int> equipment_value;
    std::optional<int> flashed;
    std::optional<int> smoked;
    std::optional<int> burning;
    std::optional<int> round_kills;
    std::optional<int> round_headshot_kills;
    std::optional<int> kills;
    std::optional<int> assists;
    std::optional<int> deaths;
    std::optional<int> mvps;
    std::optional<int> score;
};

NormalizedGameState normalize_gsi(std::string_view json_body,
                                  std::uint64_t sequence,
                                  std::uint64_t relative_us);

} // namespace cspromator
