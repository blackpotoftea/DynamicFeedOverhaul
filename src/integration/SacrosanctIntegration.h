#pragma once

// Sacrosanct Integration - Direct C++ implementation bypassing Papyrus AI-driven state
// This allows combat feeding without the player being put into AI-driven mode
namespace SacrosanctIntegration {

    // Feed context for Sacrosanct-specific handling
    struct FeedContext {
        RE::Actor* target = nullptr;
        bool isLethal = false;
        bool isSleeping = false;
        bool isSneakFeed = false;
        bool isParalyzed = false;
        bool isCombatFeed = false;
        bool isEmbrace = false;  // Sacrosanct vampire embrace feature
        bool animationHandlesKill = false;  // If true, skip ProcessLethalKill (animation has kill baked in)
    };

    // Initialize Sacrosanct integration (call during kDataLoaded)
    // Returns true if Sacrosanct is available
    bool Initialize();

    // Check if Sacrosanct is installed and available
    bool IsAvailable();

    // Process a vampire feed using direct C++ implementation
    // This replicates what Sacrosanct's ProcessFeed does WITHOUT calling StartVampireFeed
    // Returns true if feed was processed successfully
    bool ProcessFeed(const FeedContext& context);

    // Call only the pre-StartVampireFeed parts of Sacrosanct via Papyrus
    // This calls DLC1VampireTurn.PlayerBitesMe() for turning mechanics
    // Does NOT call StartVampireFeed (avoids AI-driven state)
    bool CallPreFeedPapyrus(RE::Actor* target);

    // Direct game state manipulation (vanilla vampire globals)
    // These work with or without Sacrosanct
    namespace VampireState {
        // Get current vampire hunger stage (1-4, or -1 if not vampire)
        int GetHungerStage();

        // Set vampire hunger stage directly (resets feed timer)
        // stage: 1=sated, 2=peckish, 3=hungry, 4=starving
        bool SetHungerStage(int stage);

        // Reduce hunger by one stage (e.g., 4->3, feeding effect)
        bool ReduceHunger();

        // Reset the vampire feed timer (VampireFeedTimer global)
        bool ResetFeedTimer();
    }

    // Sacrosanct-specific globals and actor values
    namespace SacrosanctState {
        // Check if Sacrosanct hemomancy (blood magic) is available
        bool HasHemomancy();

        // Add blood points for Sacrosanct blood magic
        // Returns true if successful
        bool AddBloodPoints(float amount);

        // Get current blood points
        float GetBloodPoints();

        // Check if player has specific Sacrosanct perk
        bool HasSacrosanctPerk(const char* perkEditorID);
    }

}
