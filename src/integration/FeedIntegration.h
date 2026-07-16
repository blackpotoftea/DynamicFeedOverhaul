#pragma once

// Vampire feed integration glue, shared by the legacy paired-animation path and
// the composite path. Decoupled from PairedAnimation so neither path has to
// depend on the other to fire the post-bite integration.
namespace FeedIntegration {
    // Fire the resolved vampire feed integration with the feed's FINAL lethality: the
    // SkyrimNet feed-start event (legacy path only; composite already sent it at Loop start)
    // plus the mechanical effects - the mutually-exclusive Sacrosanct/Sacrilege/BetterVampires/
    // Vanilla ProcessFeed (hunger, blood, XP, kill handling) and the vanilla OnVampireFeed
    // event - then the DFO_VampireFeed outcome event with the victim's final drained health.
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

    // Composite-only: emit ONLY the SkyrimNet feed-start event (vampire_feed) at the instant
    // drinking begins, so NPCs react as feeding starts rather than when it ends. The
    // DFO_VampireFeed outcome event and the mechanical ProcessFeed are deliberately deferred
    // to Run() (fired at MarkFeedEnded), because composite lethality is emergent - it only
    // resolves if the victim is drained dry, so committing lethal-vs-not (and the drained
    // health) here would be premature. Marks FeedAnimState's feed-start-notified flag so
    // Run() won't re-send the SkyrimNet event. Must run on the main thread.
    void RunFeedStart(RE::Actor* target, bool isLethal);
}
