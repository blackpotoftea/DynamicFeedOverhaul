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
        }
    }

    RE::BSEventNotifyControl CrosshairRefHandler::ProcessEvent(
        const SKSE::CrosshairRefEvent* event,
        [[maybe_unused]] RE::BSTEventSource<SKSE::CrosshairRefEvent>* source)
    {
        if (!event || g_clientID == 0) {
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
                    SkyPromptAPI::SendPrompt(sink, g_clientID);
                    SKSE::log::debug("Showing feed prompt for: {}", actor->GetName());
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
