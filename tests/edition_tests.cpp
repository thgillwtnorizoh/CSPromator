#include "cspromator/edition.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(1);
    }
}

void test_gsi_is_deliberately_small() {
    const auto& gsi = cspromator::edition_descriptor(cspromator::EditionKind::Gsi);
    require(gsi.id == "gsi", "GSI edition id should remain stable");
    require(gsi.providers.size() == 1, "GSI edition should contain only the GSI provider");
    require(cspromator::has_provider(gsi, "gsi"), "GSI provider should exist in GSI edition");
    require(!cspromator::has_provider(gsi, "steam-timeline"),
            "GSI edition must not silently include Steam Timeline");

    const auto active = cspromator::active_capabilities(gsi);
    require(cspromator::has_capability(active, cspromator::Capability::GsiFacts),
            "GSI edition should expose GSI facts");
    require(cspromator::has_capability(active, cspromator::Capability::DeterministicReplay),
            "GSI edition should retain deterministic replay");
    require(!cspromator::has_capability(active, cspromator::Capability::SupplementaryTeamCounts),
            "GSI edition must not claim team-count evidence");
    require(!cspromator::has_capability(active, cspromator::Capability::SteamTimelineEvents),
            "GSI edition must not claim Steam Timeline events");
}

void test_extended_is_honest_about_provider_maturity() {
    const auto& extended = cspromator::edition_descriptor(cspromator::EditionKind::Extended);
    require(extended.id == "extended", "Extended edition id should remain stable");
    require(cspromator::has_provider(extended, "gsi"), "Extended edition still requires GSI");
    require(cspromator::has_provider(extended, "visible-team-state"),
            "Extended edition should advertise visible-team-state research");
    require(cspromator::has_provider(extended, "steam-timeline"),
            "Extended edition should advertise Steam Timeline research");
    require(cspromator::has_provider(extended, "round-report"),
            "Extended edition should advertise round-report research");

    const auto active = cspromator::active_capabilities(extended);
    require(cspromator::has_capability(active, cspromator::Capability::GsiFacts),
            "Extended edition should retain the GSI factual spine");
    require(!cspromator::has_capability(active, cspromator::Capability::SupplementaryTeamCounts),
            "planned team-count provider must not become an active capability");
    require(!cspromator::has_capability(active, cspromator::Capability::SteamTimelineEvents),
            "planned Steam provider must not become an active capability");
    require(!cspromator::has_capability(active, cspromator::Capability::RoundEndReport),
            "research-only round report must not become an active capability");

    const auto potential = cspromator::potential_capabilities(extended);
    require(cspromator::has_capability(potential, cspromator::Capability::SupplementaryTeamCounts),
            "Extended edition should describe its team-count capability target");
    require(cspromator::has_capability(potential, cspromator::Capability::SteamTimelineEvents),
            "Extended edition should describe its Steam Timeline capability target");
    require(cspromator::has_capability(potential, cspromator::Capability::RoundEndReport),
            "Extended edition should describe its round-report capability target");
    require(cspromator::has_capability(potential, cspromator::Capability::CurrentRoundOdds),
            "Extended edition should retain round-odds research visibility");
    require(cspromator::has_capability(potential, cspromator::Capability::DeepStats),
            "Extended edition should retain Deep Stats research visibility");
}

void test_provider_statuses() {
    const auto& extended = cspromator::edition_descriptor(cspromator::EditionKind::Extended);
    bool saw_required_gsi = false;
    bool saw_planned_steam = false;
    bool saw_research_odds = false;

    for (const auto& provider : extended.providers) {
        if (provider.id == "gsi") {
            saw_required_gsi = provider.required && provider.status == cspromator::ProviderStatus::Active;
        } else if (provider.id == "steam-timeline") {
            saw_planned_steam = !provider.required && provider.status == cspromator::ProviderStatus::Planned;
        } else if (provider.id == "current-round-odds") {
            saw_research_odds = !provider.required &&
                provider.status == cspromator::ProviderStatus::ResearchOnly;
        }
    }

    require(saw_required_gsi, "Extended GSI provider should be required and active");
    require(saw_planned_steam, "Steam Timeline should remain planned until measured");
    require(saw_research_odds, "CurrentRoundOdds should remain research-only");
}

} // namespace

int main() {
    test_gsi_is_deliberately_small();
    test_extended_is_honest_about_provider_maturity();
    test_provider_statuses();
    std::cout << "CSPromator edition tests passed.\n";
    return 0;
}
