#include "hook.h"
#include "VampireFeedSink.h"
#include "SkyPrompt/API.hpp"

extern SkyPromptAPI::ClientID g_clientID;

namespace {
    bool IsPlayerVampire() {
        return true; // for test purpose
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        // Check for Vampire keyword (ActorTypeVampire)
        auto keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeVampire");
        if (keyword && player->HasKeyword(keyword)) {
            return true;
        }

        return false;
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

        // Check if looking at an actor and player is vampire
        if (ref && ref->Is(RE::FormType::ActorCharacter) && IsPlayerVampire()) {
            auto actor = ref->As<RE::Actor>();
            if (actor && actor != RE::PlayerCharacter::GetSingleton()) {
                // New valid target
                if (sink->GetTarget() != actor) {
                    sink->SetTarget(actor);
                    bool sent = SkyPromptAPI::SendPrompt(sink, g_clientID);
                    SKSE::log::info("Showing feed prompt for: {} (FormID: {:X}) - SendPrompt returned: {}",
                        actor->GetName(), actor->GetFormID(), sent);
                }
            }
        } else {
            // No valid target - remove prompt if we had one
            if (sink->GetTarget()) {
                SkyPromptAPI::RemovePrompt(sink, g_clientID);
                sink->SetTarget(nullptr);
                SKSE::log::debug("Removed feed prompt");
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}
