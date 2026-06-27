#pragma once

#include <functional>

// Custom paired animation feed - replaces InitiateVampireFeedPackage
namespace PairedAnimation {
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

    // Plays the chosen idle via PlayPairedFeed and, on success, dispatches the
    // post-feed integration glue (Sacrosanct / Better Vampires / werewolf branches
    // + manual-kill fallback when no OAR animation handles it).
    void ExecuteFeed(const char* idleEditorID, RE::Actor* target, bool isPairedAnim,
                     bool isLethal = false, bool hasOARAnimation = false);

    // Per-actor feed policy bundle. Centralizes mutations that were previously
    // scattered across HandleFeedAccepted, MarkFeedEnded, and OnComplete.
    struct FeedStateContext {
        RE::Actor* player;
        RE::Actor* target;
        int  feedType;
        int  targetState;       // Feed::kStanding | kCombat | kSitting | kSleeping
        bool playerInCombat;
        bool targetInCombat;
        bool enableHeightAdjust;
        bool enableRotation;
        float minHeightDiff;
        float maxHeightDiff;
    };

    // Apply all per-actor feed setup mutations (kill-move flag, pacify if combat,
    // height/rotation if standing, graph vars). Must run on main thread; both actors
    // in ctx must be alive for the duration of the call.
    void EnterFeedState(const FeedStateContext& ctx);

    // Undo everything EnterFeedState applied. Resolves target via feedTargetHandle_.
    // Safe to call even if EnterFeedState was never called (each step early-returns).
    void ExitFeedState();
}
