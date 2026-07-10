#include "PCH.h"
#include "SexLabIntegration.h"

namespace SexLabIntegration {
    namespace {
        bool g_sexlabChecked = false;
        bool g_sexlabAvailable = false;
    }

    bool Initialize() {
        if (g_sexlabChecked) {
            return g_sexlabAvailable;
        }

        g_sexlabChecked = true;

        SKSE::log::info("Checking for SexLab integration...");

        // Try to find SexLab's animating faction
        // FormID 0xE50F in SexLab.esm (Config.AnimatingFaction; SexLab's own
        // IsActorActive() checks membership in this faction)
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (dataHandler) {
            auto* faction = dataHandler->LookupForm<RE::TESFaction>(0xE50F, "SexLab.esm");
            if (faction) {
                SKSE::log::info("SexLab detected - scene exclusion enabled");
                g_sexlabAvailable = true;
                return true;
            }
        }

        SKSE::log::info("SexLab not detected - scene checking disabled");
        g_sexlabAvailable = false;
        return false;
    }

    bool IsAvailable() {
        if (!g_sexlabChecked) {
            Initialize();
        }
        return g_sexlabAvailable;
    }

    bool IsActorInScene(RE::Actor* actor) {
        if (!actor) {
            return false;
        }

        // Get the SexLab Animating Faction (FormID 0xE50F in SexLab.esm)
        // We use a static variable so we only perform the lookup once, improving performance.
        static RE::TESFaction* sexlabAnimatingFaction =
            RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESFaction>(0xE50F, "SexLab.esm");

        // If the faction was found (SexLab is installed), check if the actor is a member
        if (sexlabAnimatingFaction) {
            return actor->IsInFaction(sexlabAnimatingFaction);
        }

        return false;
    }
}
