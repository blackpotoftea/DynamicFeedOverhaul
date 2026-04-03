#include "PCH.h"
#include "VampireFeedProxyIntegration.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace VampireFeedProxyIntegration {
    static bool g_checked = false;
    static bool g_available = false;

    bool Initialize() {
        if (g_checked) return g_available;
        g_checked = true;

        // Check if VampireFeedProxy.dll is loaded as SKSE plugin
        HMODULE hModule = GetModuleHandleA("VampireFeedProxy.dll");
        g_available = (hModule != nullptr);

        SKSE::log::info("VampireFeedProxy.dll detection: {}",
            g_available ? "FOUND" : "NOT FOUND");

        return g_available;
    }

    bool IsAvailable() {
        if (!g_checked) Initialize();
        return g_available;
    }
}
