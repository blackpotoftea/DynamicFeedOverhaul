#pragma once
#include <cstdint>

namespace FeedAnimState {
    void MarkFeedStarted();
    void MarkFeedEnded();
    bool CheckAndClearFeedEnded();
    bool IsFeedActive();

    // Game-load abort: back to Idle without MarkFeedEnded's side effects;
    // also restores the global time multiplier.
    void ResetForLoad();

    // KillMoveStart animation-graph event tracking.
    // ResetKillMoveStart() clears the flag before a PlayIdle attempt; the event
    // sink calls MarkKillMoveStartSeen() when the engine fires "KillMoveStart";
    // ConsumeKillMoveStart() atomically reads-and-clears (true = event happened).
    void MarkKillMoveStartSeen();
    bool ConsumeKillMoveStart();
    void ResetKillMoveStart();

    // Lethal flag for the current feed; consulted by the VFD_VampireFeedTrigger
    // handler to choose between lethal-with-variance and fixed non-lethal drain.
    // Set in HandleFeedAccepted just before ExecuteFeed. Reset on MarkFeedStarted/Ended.
    void SetCurrentFeedLethal(bool lethal);
    bool IsCurrentFeedLethal();

    // Counts VFD_VampireFeedTrigger occurrences within a single feed so the
    // drain chunk escalates with successive bites. Reset by MarkFeedStarted.
    uint32_t IncrementVFDTriggerCount();   // returns new count starting at 1

    // "Feed actually engaged" gate for the legacy path's FeedIntegration::Run call
    // in MarkFeedEnded. Set when the legacy feed animation starts; ConsumeFeedEngaged()
    // reads-and-clears so the integration fires exactly once even if MarkFeedEnded
    // runs twice. The composite path does NOT set this — it calls FeedIntegration::Run
    // directly at Loop start, leaving the gate closed. Reset by MarkFeedStarted.
    void MarkFeedEngaged();
    bool ConsumeFeedEngaged();             // exchange(false): true if a feed had engaged
    bool HasFeedEngaged();                 // non-consuming peek; does NOT clear the gate

    // Whether the current feed used an OAR animation that bakes in the kill.
    // Consulted by the vanilla manual-kill fallback in FeedIntegration::Run.
    // Set at feed start; reset by MarkFeedStarted. Composite sets true (it owns
    // the kill itself, so the fallback must not double-kill).
    void SetFeedHasOAR(bool hasOAR);
    bool GetFeedHasOAR();

    // Victim-state snapshot taken at feed accept, while the victim is still alive and in its
    // original state. The vampire-overhaul integration runs in MarkFeedEnded AFTER the animation,
    // by which point a lethal victim is dead - so IsInCombat()/IsSleeping() can't be re-derived
    // there (they'd read false and silently skip the Blood Knight stamina cost, Kiss of Death,
    // Lamae's Wrath, etc.). Set in HandleFeedAccepted; reset by MarkFeedStarted.
    void SetFeedInCombat(bool inCombat);
    bool GetFeedInCombat();
    void SetFeedSleeping(bool sleeping);
    bool GetFeedSleeping();

    // Set by FeedIntegration::RunFeedStart when the composite path emits the feed-start
    // narrative events (DFO_VampireFeed + SkyrimNet) at Loop start. Read by
    // FeedIntegration::Run (fired from MarkFeedEnded with the FINAL lethality) so those
    // events aren't re-sent for composite. The legacy path leaves this false and Run
    // sends them once at feed end. Reset by MarkFeedStarted.
    void SetFeedStartNotified(bool notified);
    bool GetFeedStartNotified();
}
