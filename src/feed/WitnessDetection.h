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
}
