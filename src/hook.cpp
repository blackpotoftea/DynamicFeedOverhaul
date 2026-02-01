#include "hook.h"
#include "VampireFeedSink.h"
#include "Settings.h"
#include "PapyrusCall.h"
#include "SkyPrompt/API.hpp"

extern SkyPromptAPI::ClientID g_clientID;

namespace {
    // Check if player can feed (vampire check + optional hunger check)
    // targetInCombat: if true and Combat.IgnoreHungerCheck is set, bypasses hunger requirement
    bool CanPlayerFeed(bool targetInCombat) {
        auto* settings = Settings::GetSingleton();

        // Debug override - always return true if ForceVampire enabled
        if (settings->General.ForceVampire) {
            SKSE::log::debug("ForceVampire enabled - bypassing vampire check");
            return true;
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        // Check for Vampire keyword (ActorTypeVampire)
        auto keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeVampire");
        bool hasKeyword = keyword && player->HasKeyword(keyword);

        if (!hasKeyword) {
            SKSE::log::debug("Player is not a vampire (no ActorTypeVampire keyword)");
            return false;
        }

        // Optional: Check hunger stage requirement
        if (settings->General.CheckHungerStage) {
            // Combat targets can bypass hunger check if IgnoreHungerCheck is enabled
            if (targetInCombat && settings->Combat.IgnoreHungerCheck) {
                SKSE::log::debug("Combat target - ignoring hunger check (IgnoreHungerCheck=true)");
                return true;
            }

            int vampireStage = PapyrusCall::GetVampireStage();
            if (vampireStage < settings->General.MinHungerStage) {
                SKSE::log::debug("Vampire hunger stage {} < required {} - feeding not allowed",
                    vampireStage, settings->General.MinHungerStage);
                return false;
            }
            SKSE::log::debug("Vampire hunger stage {} >= {} - feeding allowed",
                vampireStage, settings->General.MinHungerStage);
        }

        return true;
    }
}

namespace Hooks {
    void Install() {
        auto crosshairSource = SKSE::GetCrosshairRefEventSource();
        if (crosshairSource) {
            crosshairSource->AddEventSink(CrosshairRefHandler::GetSingleton());
            SKSE::log::info("Crosshair ref event handler registered");
        } else {
            SKSE::log::error("Failed to get crosshair event source!");
        }
    }

    RE::BSEventNotifyControl CrosshairRefHandler::ProcessEvent(
        const SKSE::CrosshairRefEvent* event,
        [[maybe_unused]] RE::BSTEventSource<SKSE::CrosshairRefEvent>* source)
    {
        if (!event) {
            SKSE::log::warn("CrosshairRefEvent is null");
            return RE::BSEventNotifyControl::kContinue;
        }

        if (g_clientID == 0) {
            SKSE::log::warn("ClientID is 0, SkyPrompt not initialized");
            return RE::BSEventNotifyControl::kContinue;
        }

        auto sink = VampireFeedSink::GetSingleton();
        auto ref = event->crosshairRef.get();

        // Check if looking at a valid feed target
        bool isValidTarget = false;
        RE::Actor* actor = nullptr;

        if (ref && ref->Is(RE::FormType::ActorCharacter)) {
            actor = ref->As<RE::Actor>();
            if (actor && actor != RE::PlayerCharacter::GetSingleton()) {
                // Check vampire status with target's combat state for hunger bypass
                bool targetInCombat = actor->IsInCombat();
                if (CanPlayerFeed(targetInCombat) && !VampireFeedSink::IsExcluded(actor)) {
                    isValidTarget = true;
                }
            }
        }

        if (isValidTarget && actor) {
            if (sink->GetTarget() != actor) {
                sink->SetTarget(actor);
                bool sent = SkyPromptAPI::SendPrompt(sink, g_clientID);
                SKSE::log::info("Showing feed prompt for: {} (FormID: {:X}) - SendPrompt returned: {}",
                    actor->GetName(), actor->GetFormID(), sent);
            }
        } else {
            // No valid target or excluded - remove prompt if we had one
            if (sink->GetTarget()) {
                SkyPromptAPI::RemovePrompt(sink, g_clientID);
                sink->SetTarget(nullptr);
                SKSE::log::debug("Removed feed prompt");
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
