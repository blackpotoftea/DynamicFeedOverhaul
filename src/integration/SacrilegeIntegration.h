#pragma once

// Sacrilege Integration - Direct C++ implementation for combat feeding
namespace SacrilegeIntegration {

    // Feed context for Sacrilege-specific handling
    struct FeedContext {
        RE::Actor* target = nullptr;
        bool isLethal = false;
        bool isSleeping = false;
        bool isSneakFeed = false;
        bool isParalyzed = false;
        bool isCombatFeed = false;
        bool isEmbrace = false;
        bool animationHandlesKill = false;  // If true, skip kill (animation has kill baked in)
    };

    // Initialize Sacrilege integration (call during kDataLoaded)
    bool Initialize();

    // Check if Sacrilege is installed and available
    bool IsAvailable();

    // Process a vampire feed using direct C++ implementation
    bool ProcessFeed(const FeedContext& context);
}
