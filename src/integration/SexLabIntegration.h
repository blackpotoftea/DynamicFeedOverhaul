#pragma once

namespace SexLabIntegration {
    // Initialize SexLab integration (call during kDataLoaded)
    // Returns true if SexLab is available, false otherwise
    bool Initialize();

    // Check if SexLab is installed and available
    bool IsAvailable();

    // Check if an actor is currently in a SexLab scene
    // Returns false if SexLab is not available or actor is not in a scene
    bool IsActorInScene(RE::Actor* actor);
}
