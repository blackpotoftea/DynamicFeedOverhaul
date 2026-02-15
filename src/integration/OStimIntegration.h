#pragma once

namespace OStimIntegration {
    // Initialize OStim integration (call during kDataLoaded)
    // Returns true if OStim is available, false otherwise
    bool Initialize();

    // Check if OStim is installed and available
    bool IsAvailable();

    // Check if an actor is currently in an OStim scene
    // Returns false if OStim is not available or actor is not in a scene
    bool IsActorInScene(RE::Actor* actor);
}
