#include "cspromator/edition.hpp"

#include <array>

namespace cspromator {
namespace {

constexpr std::array kCapabilities{
    CapabilityDescriptor{Capability::GsiFacts,
                         "gsi-facts",
                         "Factual local-player, round, bomb and lifecycle state from CS2 GSI."},
    CapabilityDescriptor{Capability::DeterministicReplay,
                         "deterministic-replay",
                         "Record and replay accepted telemetry in canonical ingress order."},
    CapabilityDescriptor{Capability::SupplementaryTeamCounts,
                         "supplementary-team-counts",
                         "Player-visible CT/T alive and total counts for evidence fusion."},
    CapabilityDescriptor{Capability::SteamTimelineEvents,
                         "steam-timeline-events",
                         "Valve Steam Timeline semantic event enrichment."},
    CapabilityDescriptor{Capability::RoundEndReport,
                         "round-end-report",
                         "Historical per-round analytical report data."},
    CapabilityDescriptor{Capability::CurrentRoundOdds,
                         "current-round-odds",
                         "Current round win-odds telemetry; research-only until trust semantics are known."},
    CapabilityDescriptor{Capability::DeepStats,
                         "deep-stats",
                         "Historical Deep Stats / 360-style match analytics."},
};

constexpr Capability kGsiCapabilities =
    Capability::GsiFacts | Capability::DeterministicReplay;

constexpr std::array kGsiProviders{
    ProviderDescriptor{
        "gsi",
        "Game State Integration",
        ProviderStatus::Active,
        true,
        kGsiCapabilities,
        "Required factual spine; loopback HTTP only.",
    },
};

constexpr std::array kExtendedProviders{
    ProviderDescriptor{
        "gsi",
        "Game State Integration",
        ProviderStatus::Active,
        true,
        kGsiCapabilities,
        "Required factual spine; Extended must degrade to this provider alone.",
    },
    ProviderDescriptor{
        "visible-team-state",
        "Visible Team State",
        ProviderStatus::Planned,
        false,
        Capability::SupplementaryTeamCounts,
        "Provider contract exists; no supported live external bridge is enabled yet.",
    },
    ProviderDescriptor{
        "steam-timeline",
        "Steam Timeline",
        ProviderStatus::Planned,
        false,
        Capability::SteamTimelineEvents,
        "Awaiting measured live-write behavior of Steam Game Recording timeline metadata.",
    },
    ProviderDescriptor{
        "round-report",
        "CS2 Round End Report",
        ProviderStatus::ResearchOnly,
        false,
        Capability::RoundEndReport,
        "Current CS2 protocol contains the data, but no supported external bridge is known.",
    },
    ProviderDescriptor{
        "current-round-odds",
        "CS2 Current Round Odds",
        ProviderStatus::ResearchOnly,
        false,
        Capability::CurrentRoundOdds,
        "Quarantined from Director use until its information inputs and trust boundary are understood.",
    },
    ProviderDescriptor{
        "deep-stats",
        "CS2 Deep Stats",
        ProviderStatus::ResearchOnly,
        false,
        Capability::DeepStats,
        "Historical analytics only; not part of the live Director path.",
    },
};

const EditionDescriptor kGsiEdition{
    EditionKind::Gsi,
    "gsi",
    "CSPromator GSI",
    "cspromator-gsi",
    kGsiProviders,
};

const EditionDescriptor kExtendedEdition{
    EditionKind::Extended,
    "extended",
    "CSPromator Extended",
    "cspromator-extended",
    kExtendedProviders,
};

} // namespace

const EditionDescriptor& edition_descriptor(EditionKind kind) {
    return kind == EditionKind::Extended ? kExtendedEdition : kGsiEdition;
}

std::span<const CapabilityDescriptor> capability_catalog() {
    return kCapabilities;
}

Capability active_capabilities(const EditionDescriptor& edition) {
    Capability result = Capability::None;
    for (const auto& provider : edition.providers) {
        if (provider.status == ProviderStatus::Active) {
            result |= provider.capabilities;
        }
    }
    return result;
}

Capability potential_capabilities(const EditionDescriptor& edition) {
    Capability result = Capability::None;
    for (const auto& provider : edition.providers) {
        result |= provider.capabilities;
    }
    return result;
}

bool has_provider(const EditionDescriptor& edition, std::string_view provider_id) {
    for (const auto& provider : edition.providers) {
        if (provider.id == provider_id) return true;
    }
    return false;
}

std::string_view to_string(EditionKind kind) {
    switch (kind) {
        case EditionKind::Gsi: return "gsi";
        case EditionKind::Extended: return "extended";
    }
    return "unknown";
}

std::string_view to_string(ProviderStatus status) {
    switch (status) {
        case ProviderStatus::Active: return "active";
        case ProviderStatus::Planned: return "planned";
        case ProviderStatus::ResearchOnly: return "research-only";
    }
    return "unknown";
}

} // namespace cspromator
