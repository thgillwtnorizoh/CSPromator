#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace cspromator {

enum class EditionKind {
    Gsi,
    Extended,
};

enum class ProviderStatus {
    Active,
    Planned,
    ResearchOnly,
};

enum class Capability : std::uint32_t {
    None = 0,
    GsiFacts = 1U << 0,
    DeterministicReplay = 1U << 1,
    SupplementaryTeamCounts = 1U << 2,
    SteamTimelineEvents = 1U << 3,
    RoundEndReport = 1U << 4,
    CurrentRoundOdds = 1U << 5,
    DeepStats = 1U << 6,
};

constexpr Capability operator|(Capability lhs, Capability rhs) {
    return static_cast<Capability>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr Capability operator&(Capability lhs, Capability rhs) {
    return static_cast<Capability>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr Capability& operator|=(Capability& lhs, Capability rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool has_capability(Capability capabilities, Capability capability) {
    return (capabilities & capability) != Capability::None;
}

struct CapabilityDescriptor {
    Capability capability{Capability::None};
    std::string_view id;
    std::string_view description;
};

struct ProviderDescriptor {
    std::string_view id;
    std::string_view display_name;
    ProviderStatus status{ProviderStatus::Planned};
    bool required{false};
    Capability capabilities{Capability::None};
    std::string_view note;
};

struct EditionDescriptor {
    EditionKind kind{EditionKind::Gsi};
    std::string_view id;
    std::string_view display_name;
    std::string_view executable_name;
    std::span<const ProviderDescriptor> providers;
};

const EditionDescriptor& edition_descriptor(EditionKind kind);
std::span<const CapabilityDescriptor> capability_catalog();
Capability active_capabilities(const EditionDescriptor& edition);
Capability potential_capabilities(const EditionDescriptor& edition);
bool has_provider(const EditionDescriptor& edition, std::string_view provider_id);
std::string_view to_string(EditionKind kind);
std::string_view to_string(ProviderStatus status);

} // namespace cspromator
