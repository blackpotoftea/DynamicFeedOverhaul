#pragma once
#include <cstdint>

namespace FeedAnimState {
    void MarkFeedStarted();
    void MarkFeedEnded();
    bool CheckAndClearFeedEnded();
    bool IsFeedActive();

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
}
