#pragma once

namespace PoiseIntegration {
    // Initialize Poise integration (call during kDataLoaded)
    // Returns true if a poise mod is available, false otherwise
    bool Initialize();

    // Check if a poise mod is installed and available
    bool IsAvailable();
}
