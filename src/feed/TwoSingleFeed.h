#pragma once

#include "RE/Skyrim.h"

// Two single-actor animation feed system
// Alternative to paired animations - triggers separate animations on player and target
// Positioning based on OStim pattern: center point + per-actor offsets
namespace TwoSingleFeed {

    // Main entry point - play two single-actor feed animations
    bool PlayTwoSingleFeed(RE::Actor* target);

    // Called when feed animation completes normally
    void OnComplete();

    // Force stop
    void ForceStop();

    // Check if active
    bool IsActive();

    // Get current target
    RE::NiPointer<RE::Actor> GetFeedTarget();
}
