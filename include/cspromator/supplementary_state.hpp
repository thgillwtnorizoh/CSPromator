#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cspromator {

enum class SupplementarySourceKind {
    Unknown,
    Synthetic,
    Replay,
    PanoramaVisibleState,
};

struct TeamCounts {
    std::optional<int> ct_alive;
    std::optional<int> t_alive;
    std::optional<int> ct_total;
    std::optional<int> t_total;
};

struct SupplementarySnapshot {
    std::uint64_t sequence{};
    std::uint64_t observed_tick{};
    std::uint64_t relative_us{};

    // A source increments this whenever team membership changes. Alive-count
    // changes alone must not increment it. Keeping this explicit lets the
    // resolver notice roster churn even if totals later return to the same
    // numeric value.
    std::optional<std::uint64_t> roster_revision;

    SupplementarySourceKind source{SupplementarySourceKind::Unknown};
    TeamCounts teams;
};

struct PerspectiveTeamCounts {
    std::optional<int> local_alive;
    std::optional<int> enemy_alive;
    std::optional<int> local_total;
    std::optional<int> enemy_total;
};

std::string_view to_string(SupplementarySourceKind source);
std::optional<SupplementarySourceKind> supplementary_source_from_string(std::string_view value);
bool valid_team_counts(const TeamCounts& counts);
std::optional<PerspectiveTeamCounts> perspective_for_team(
    const TeamCounts& counts,
    std::string_view local_team);

} // namespace cspromator
