#pragma once

// Vampire feed integration glue, shared by the legacy paired-animation path and
// the composite path. Decoupled from PairedAnimation so neither path has to
// depend on the other to fire the post-bite integration.
namespace FeedIntegration {
    // Fire the resolved vampire feed integration with the feed's FINAL lethality: the
    // feed-start narrative events (custom DFO_VampireFeed + SkyrimNet) plus the mechanical
    // effects - the mutually-exclusive Sacrosanct/Sacrilege/BetterVampires/Vanilla
    // ProcessFeed (hunger, blood, XP, kill handling) and the vanilla OnVampireFeed event.
    // Also handles werewolf corpse feeding.
    //
    // Called once per feed from FeedAnimState::MarkFeedEnded() for BOTH paths, when the
    // outcome is known. If RunFeedStart already emitted the narrative events for this feed
    // (composite path), Run skips them and applies only the mechanical effects. Must run
    // on the main thread.
    //
    // hasOARAnimation=true suppresses the vanilla manual-kill fallback (the OAR clip
    // or the composite Loop owns the kill).
    void Run(RE::Actor* target, bool isLethal, bool hasOARAnimation);

    // Composite-only: emit ONLY the feed-start narrative events (DFO_VampireFeed +
    // SkyrimNet vampire_feed) at the instant drinking begins, so NPCs react as feeding
    // starts rather than when it ends. The mechanical ProcessFeed is deliberately deferred
    // to Run() (fired at MarkFeedEnded), because composite lethality is emergent - it only
    // resolves if the victim is drained dry, so committing lethal-vs-not here would be
    // premature. Marks FeedAnimState's feed-start-notified flag so Run() won't re-send
    // these events. Must run on the main thread.
    void RunFeedStart(RE::Actor* target, bool isLethal);
}
