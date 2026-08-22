#pragma once

#include "cspromator/event_detector.hpp"
#include "cspromator/game_state.hpp"
#include "cspromator/supplementary_state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cspromator {

struct SemanticResolverConfig {
    // Intentionally unset until a real supplementary provider has been profiled.
    // A future provider may set this after its update cadence is measured.
    std::optional<std::uint64_t> max_supplement_age_us;

    // Confirming an ace requires proof that team membership did not churn.
    bool require_roster_revision_for_ace{true};
};

struct SemanticContext {
    bool supplement_available{false};
    SupplementarySourceKind supplement_source{SupplementarySourceKind::Unknown};
    std::uint64_t supplement_relative_us{};

    std::optional<int> local_alive;
    std::optional<int> enemy_alive;
    std::optional<int> local_total;
    std::optional<int> enemy_total;
    std::optional<int> alive_delta;

    bool clutch_active{false};
    std::optional<int> clutch_opponents;
    bool roster_changed_this_round{false};
};

class SemanticResolver {
public:
    explicit SemanticResolver(SemanticResolverConfig config = {});

    // GSI remains the factual spine. This call consumes the already-derived
    // factual batch and may add higher-level semantic events.
    std::vector<PromatorEvent> process_gsi(
        const NormalizedGameState& state,
        const std::vector<PromatorEvent>& factual_events);

    // Supplementary state is a separate, optional ingress. It may update
    // semantic context or emit semantics without requiring another GSI packet.
    std::vector<PromatorEvent> process_supplementary(
        const SupplementarySnapshot& snapshot);

    void reset();
    const SemanticContext& context() const noexcept { return context_; }

private:
    bool snapshot_fresh_at(const SupplementarySnapshot& snapshot,
                           std::uint64_t reference_us) const;
    void begin_round(const NormalizedGameState& state);
    void establish_round_baseline(const SupplementarySnapshot& snapshot);
    void update_context(const SupplementarySnapshot& snapshot);
    void observe_roster(const SupplementarySnapshot& snapshot);

    std::vector<PromatorEvent> evaluate_clutch(std::uint64_t event_us);
    std::vector<PromatorEvent> evaluate_ace(std::uint64_t event_us);
    std::optional<PromatorEvent> end_clutch(std::uint64_t event_us);
    PromatorEvent make_semantic_event(EventType type,
                                      std::uint64_t event_us) const;

    SemanticResolverConfig config_;
    SemanticContext context_;

    std::optional<NormalizedGameState> latest_state_;
    std::optional<SupplementarySnapshot> latest_supplement_;
    std::optional<std::string> local_team_;
    std::optional<bool> local_player_alive_;

    bool round_has_known_start_{false};
    bool round_ended_{false};
    std::uint64_t round_start_us_{};
    std::optional<int> initial_enemy_total_;
    std::optional<std::uint64_t> initial_roster_revision_;
    bool roster_changed_this_round_{false};
    bool ace_emitted_this_round_{false};

    bool clutch_active_{false};
    std::optional<int> last_clutch_enemy_alive_;
    std::optional<std::uint64_t> last_supplement_us_;
};

} // namespace cspromator
