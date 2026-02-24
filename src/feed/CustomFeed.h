#pragma once

// Custom paired animation feed - replaces InitiateVampireFeedPackage
namespace CustomFeed {
    // Feed target management - uses ObjectRefHandle for safe persistence
    void SetFeedTarget(RE::Actor* target);
    void ClearFeedTarget();
    RE::NiPointer<RE::Actor> GetFeedTarget();

    // Utility functions
    bool IsPlayerOnLeftSide(RE::Actor* target);
    bool IsBedroll(RE::TESObjectREFR* furniture);

    // Animation playback
    bool PlayPairedFeed(const char* idleEditorID, RE::Actor* target, bool isPaired = true);
    void ForceStop();
    void OnComplete();
}
