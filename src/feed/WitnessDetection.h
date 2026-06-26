#pragma once

namespace RE {
    class Actor;
}

namespace WitnessDetection {
    // Check if any nearby NPCs can see and have detected the feeding
    // Returns the first witness found, or nullptr if no witnesses
    RE::Actor* CheckForWitnesses(RE::Actor* player, RE::Actor* target);

    // Check if a specific actor can detect the feeding
    bool CanActorWitnessFeed(RE::Actor* potentialWitness, RE::Actor* player, RE::Actor* target);

    // Get detection level of an actor toward the player
    // Returns: 0=None, 1=Noticed, 2=Lost, 3=Seen
    std::int32_t GetDetectionLevel(RE::Actor* detector, RE::Actor* target);

    // Perform witness check during active feed and handle detection
    // Should be called periodically (e.g., every 0.5 seconds) during feed
    void PerformWitnessCheck(RE::Actor* player, RE::Actor* target);

    // Handle witness detection - adds bounty and notifies player
    void OnDetectedByWitness(RE::Actor* player, RE::Actor* target, RE::Actor* witness);

    // Reset the per-feed "already reported" latch. Call once at the start of every feed so
    // each public feed registers exactly one assault charge - the witness check runs every
    // tick during the feed and would otherwise re-add the bounty repeatedly.
    void ResetFeedReport();

    // True once this feed's witness outcome has been settled (crime charged OR determined
    // not a reportable crime). The update hook uses this to stop calling PerformWitnessCheck
    // for the rest of the feed - nothing changes after the verdict.
    bool IsFeedReported();

    // Called once at feed end: evaluates the victim and any bystanders who saw the
    // feed against the relationship/confidence model and starts combat (deferred) on
    // those who turn hostile. The bounty/report side is handled live during the feed
    // by PerformWitnessCheck. Gated by Combat.EnableWitnessCombatReaction.
    void ApplyWitnessReactions(RE::Actor* player, RE::Actor* victim);
}
