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

    // Live BV hunger state for the debug UI (reads globals + bound-script properties).
    // valid=false means forms/script aren't resolved yet (no feed this session).
    struct HungerDebug {
        bool valid = false;
        bool bloodPointsMode = false;   // EnableVampireBloodPoints == 10000
        int stageMode = 0;              // VampireDynamicStages: 0 normal / 10000 dynamic / 20000 two-stage
        float feedReady = -1.0f;        // VampireFeedReady global (hunger stage the game reads, 0-3)
        float vampireStatus = -1.0f;    // VampireStatus script property (1-4)
        float bloodPoints = -1.0f;      // VampireBloodPoints global
        float feedTimer = -1.0f;        // FeedTimer script property (game-days since last feed)
        float lastFeedTime = -1.0f;     // LastFeedTime script property
        float gameDaysPassed = -1.0f;   // GameDaysPassed global (now)
        bool feedTimerEnabled = false;  // BVCalculateFeedTimer > 0 (else FeedTimer never updates)
        bool updateGated = false;       // VampireUpdateGameTime != 0 (blocks stage updates)
    };
    HungerDebug GetHungerDebug();

    // Process a vampire feed using direct C++ implementation
    bool ProcessFeed(const FeedContext& context);
}
