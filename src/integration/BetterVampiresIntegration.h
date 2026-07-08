#pragma once

// Better Vampires Integration - Direct C++ implementation of the post-feed logic
namespace BetterVampiresIntegration {

    // Feed context for Better Vampires-specific handling
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

    // Initialize Better Vampires integration (call during kDataLoaded)
    bool Initialize();

    // Check if Better Vampires is installed and available
    bool IsAvailable();

    // Detected BV version classification for logs/debug UI ("9.1+", "8.9 or older", ...)
    const char* GetVersionInfo();

    // Process a vampire feed using direct C++ implementation
    bool ProcessFeed(const FeedContext& context);
}
