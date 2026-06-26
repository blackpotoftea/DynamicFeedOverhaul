#pragma once

// Vampire feed integration glue, shared by the legacy paired-animation path and
// the composite path. Decoupled from PairedAnimation so neither path has to
// depend on the other to fire the post-bite integration.
namespace FeedIntegration {
    // Fire the resolved vampire feed integration: the vanilla OnVampireFeed event,
    // the mod's custom DAO_VampireFeed event, and the (mutually exclusive)
    // Sacrosanct/Sacrilege/BetterVampires/Vanilla ProcessFeed (hunger, blood, XP,
    // kill handling). Also handles werewolf corpse feeding.
    //
    // Called once per feed: from FeedAnimState::MarkFeedEnded() on the legacy path,
    // and from CompositePairedAnimation::AdvanceToLoop() (Loop start) on the
    // composite path. Must run on the main thread.
    //
    // hasOARAnimation=true suppresses the vanilla manual-kill fallback (the OAR clip
    // or the composite Loop owns the kill).
    void Run(RE::Actor* target, bool isLethal, bool hasOARAnimation);
}
