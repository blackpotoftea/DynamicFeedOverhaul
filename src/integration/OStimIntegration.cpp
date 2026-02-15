#include "PCH.h"
#include "OStimIntegration.h"

namespace OStimIntegration {
    namespace {
        bool g_ostimChecked = false;
        bool g_ostimAvailable = false;
    }

    bool Initialize() {
        if (g_ostimChecked) {
            return g_ostimAvailable;
        }

        g_ostimChecked = true;

        SKSE::log::info("Checking for OStim NG integration...");

        // Try to find OStim's excitement faction
        // FormID 0xD93 in OStim.esp
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (dataHandler) {
            auto* faction = dataHandler->LookupForm<RE::TESFaction>(0xD93, "OStim.esp");
            if (faction) {
                SKSE::log::info("OStim NG detected - scene exclusion enabled");
                g_ostimAvailable = true;
                return true;
            }
        }

        SKSE::log::info("OStim NG not detected - scene checking disabled");
        g_ostimAvailable = false;
        return false;
    }

    bool IsAvailable() {
        if (!g_ostimChecked) {
            Initialize();
        }
        return g_ostimAvailable;
    }

    bool IsActorInScene(RE::Actor* actor) {
        if (!actor) {
            return false;
        }

        // Get the OStim Excitement Faction (FormID 0xD93 in OStim.esp)
        // We use a static variable so we only perform the lookup once, improving performance.
        static RE::TESFaction* ostimExcitementFaction =
            RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESFaction>(0xD93, "OStim.esp");

        // If the faction was found (OStim is installed), check if the actor is a member
        if (ostimExcitementFaction) {
            return actor->IsInFaction(ostimExcitementFaction);
        }

        return false;
    }
}
