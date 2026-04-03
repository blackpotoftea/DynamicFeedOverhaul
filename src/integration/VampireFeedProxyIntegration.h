#pragma once

namespace VampireFeedProxyIntegration {
    // Check if VampireFeedProxy.dll is loaded (SKSE plugin detection)
    bool Initialize();
    bool IsAvailable();
}
