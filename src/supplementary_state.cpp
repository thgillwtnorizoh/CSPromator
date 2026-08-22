#include "cspromator/supplementary_state.hpp"

namespace cspromator {
namespace {

bool valid_nonnegative(const std::optional<int>& value) {
    return !value || *value >= 0;
}

bool alive_fits_total(const std::optional<int>& alive,
                      const std::optional<int>& total) {
    return !alive || !total || *alive <= *total;
}

} // namespace

std::string_view to_string(SupplementarySourceKind source) {
    switch (source) {
        case SupplementarySourceKind::Unknown: return "unknown";
        case SupplementarySourceKind::Synthetic: return "synthetic";
        case SupplementarySourceKind::Replay: return "replay";
        case SupplementarySourceKind::PanoramaVisibleState: return "panorama-visible-state";
    }
    return "unknown";
}

std::optional<SupplementarySourceKind> supplementary_source_from_string(std::string_view value) {
    if (value == "unknown") return SupplementarySourceKind::Unknown;
    if (value == "synthetic") return SupplementarySourceKind::Synthetic;
    if (value == "replay") return SupplementarySourceKind::Replay;
    if (value == "panorama-visible-state") return SupplementarySourceKind::PanoramaVisibleState;
    return std::nullopt;
}

bool valid_team_counts(const TeamCounts& counts) {
    return valid_nonnegative(counts.ct_alive) &&
           valid_nonnegative(counts.t_alive) &&
           valid_nonnegative(counts.ct_total) &&
           valid_nonnegative(counts.t_total) &&
           alive_fits_total(counts.ct_alive, counts.ct_total) &&
           alive_fits_total(counts.t_alive, counts.t_total);
}

std::optional<PerspectiveTeamCounts> perspective_for_team(
    const TeamCounts& counts,
    std::string_view local_team) {
    if (!valid_team_counts(counts)) {
        return std::nullopt;
    }

    PerspectiveTeamCounts result;
    if (local_team == "CT") {
        result.local_alive = counts.ct_alive;
        result.enemy_alive = counts.t_alive;
        result.local_total = counts.ct_total;
        result.enemy_total = counts.t_total;
        return result;
    }
    if (local_team == "T") {
        result.local_alive = counts.t_alive;
        result.enemy_alive = counts.ct_alive;
        result.local_total = counts.t_total;
        result.enemy_total = counts.ct_total;
        return result;
    }
    return std::nullopt;
}

} // namespace cspromator
