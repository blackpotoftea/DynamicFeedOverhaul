#pragma once

#include <functional>

// Custom paired animation feed - replaces InitiateVampireFeedPackage
namespace CustomFeed {
    // Callback type for PlayPairedFeed result - called on game thread after PlayIdle attempt
    // Parameters: success (true if animation started), target actor (may be null for non-paired)
    using FeedCallback = std::function<void(bool success, RE::Actor* target)>;

    // Feed target management - uses ObjectRefHandle for safe persistence
    void SetFeedTarget(RE::Actor* target);
    void ClearFeedTarget();
    RE::NiPointer<RE::Actor> GetFeedTarget();

    // Utility functions
    bool IsPlayerOnLeftSide(RE::Actor* target);
    bool IsBedroll(RE::TESObjectREFR* furniture);

    // Animation playback
    // callback is invoked on game thread after animation starts (or fails)
    void PlayPairedFeed(const char* idleEditorID, RE::Actor* target, bool isPaired = true,
                        FeedCallback callback = nullptr);
    void ForceStop();
    void OnComplete();
}
