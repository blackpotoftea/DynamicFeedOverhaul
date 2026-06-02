#pragma once

#include "RE/Skyrim.h"

// Simulates a paired animation by playing two single-actor idles in sync,
// positioned around a shared scene center (OStim pattern: center + per-actor offsets).
namespace CompositePairedAnimation {

    // Main entry point - play two single-actor feed animations
    bool Play(RE::Actor* target);

    // Called when feed animation completes normally
    void OnComplete();

    // Force stop
    void ForceStop();

    // Check if active
    bool IsActive();

    // Get current target
    RE::NiPointer<RE::Actor> GetFeedTarget();

    // Per-frame position lock — drives the target to the offset configured in
    // Settings::NonCombat (face-opposite from player). Called once per frame
    // from PlayerUpdateHook. No-op when IsActive() is false, so cost is a
    // single bool check during normal gameplay.
    void Tick();
}
