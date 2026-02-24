#include "PCH.h"
#include "PoiseIntegration.h"

namespace PoiseIntegration {
    namespace {
        bool g_poiseChecked = false;
        bool g_poiseAvailable = false;
    }

    bool Initialize() {
        if (g_poiseChecked) {
            return g_poiseAvailable;
        }

        g_poiseChecked = true;

        SKSE::log::info("Checking for Poise mod integration...");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::warn("DataHandler not available - cannot check for poise mods");
            g_poiseAvailable = false;
            return false;
        }

        // Check for ChocolatePoise - look for its ESP/ESL
        if (dataHandler->LookupModByName("ChocolatePoise.esp")) {
            SKSE::log::info("ChocolatePoise detected - level check bypassed for stagger feeding");
            g_poiseAvailable = true;
            return true;
        }

        // Check for Loki's Poise mod
        if (dataHandler->LookupModByName("loki_POISE.esp")) {
            SKSE::log::info("Loki's Poise mod detected - level check bypassed for stagger feeding");
            g_poiseAvailable = true;
            return true;
        }

        // Check for POISE - Stagger Overhaul (common alternative)
        if (dataHandler->LookupModByName("POISE - Stagger Overhaul.esp")) {
            SKSE::log::info("POISE - Stagger Overhaul detected - level check bypassed for stagger feeding");
            g_poiseAvailable = true;
            return true;
        }

        SKSE::log::info("Poise mod not found - using vanilla stagger level requirements");
        g_poiseAvailable = false;
        return false;
    }

    bool IsAvailable() {
        if (!g_poiseChecked) {
            Initialize();
        }
        return g_poiseAvailable;
    }
}
