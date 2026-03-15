#include "utils/log.h"
#include "hooks/hook.h"
#include "Settings.h"
#include "feed/PairedAnimPromptSink.h"
#include "SkyPrompt/API.hpp"
#include "SKSEMCP/SKSEMenuFramework.hpp"
#include "feed/FeedIconOverlay.h"
#include "integration/OStimIntegration.h"
#include "integration/PoiseIntegration.h"
#include "integration/SacrosanctIntegration.h"
#include "integration/SacrilegeIntegration.h"
#include "feed/AnimationRegistry.h"
#include "utils/FormUtils.h"
#include "utils/AnimUtil.h"

std::atomic<SkyPromptAPI::ClientID> g_clientID{0};

void OnDataLoaded()
{
    g_clientID.store(SkyPromptAPI::RequestClientID(), std::memory_order_release);
    if (g_clientID.load(std::memory_order_acquire) == 0) {
        SKSE::log::error("Failed to obtain SkyPrompt ClientID - SkyPrompt mod is not installed. Initialization aborted");
        return;
    }

    SKSE::log::info("Obtained SkyPrompt ClientID: {}", g_clientID.load(std::memory_order_acquire));


    FormUtils::InitializeCache();
    Feed::AnimationRegistry::GetSingleton()->LoadAnimations("Data/SKSE/Plugins");

    if (OStimIntegration::Initialize()) {
        SKSE::log::info("OStim NG integration initialized successfully");
    } else {
        SKSE::log::info("OStim NG not detected - scene exclusion will be skipped");
    }

    if (PoiseIntegration::Initialize()) {
        SKSE::log::info("Poise mod integration initialized successfully");
    } else {
        SKSE::log::info("Poise mod not detected - using vanilla stagger behavior");
    }

    // Vampire overhaul integrations
    bool hasSacrosanct = SacrosanctIntegration::Initialize();
    bool hasSacrilege = SacrilegeIntegration::Initialize();

    if (hasSacrosanct) {
        SKSE::log::info("Sacrosanct detected");
        SacrosanctIntegration::RegisterEmbracePrompt();
    }
    if (hasSacrilege) SKSE::log::info("Sacrilege detected");

    if (!hasSacrosanct && !hasSacrilege) {
        SKSE::log::info("No vampire overhaul detected - using vanilla vampire feed system");
    } else if (hasSacrosanct && hasSacrilege) {
        SKSE::log::warn("Multiple vampire overhauls detected - this may cause conflicts!");
    }

    if (SKSEMenuFramework::IsInstalled()) {
        SKSE::log::info("SKSEMenuFramework detected, registering icon overlay as HUD element");

        SKSEMenuFramework::AddHudElement([]() {
            FeedIconOverlay::GetSingleton()->RenderOverlay();
        });
        SKSE::log::info("Successfully registered icon overlay HUD element with SKSEMenuFramework");
    } else {
        SKSE::log::error("SKSEMenuFramework not found - icon overlay will not render");
    }

    Hooks::Install();
    SKSE::log::info("Mod initialization complete");
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		OnDataLoaded();
		break;
	case SKSE::MessagingInterface::kPostLoad:
		break;
	case SKSE::MessagingInterface::kPreLoadGame:
		// Unregister animation sink before loading (safety - prevents crash if mod removed)
		AnimEventSink::Unregister();
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
        break;
	case SKSE::MessagingInterface::kNewGame:
		break;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
	SetupLog();

    SKSE::log::info("Dynamic Feed Overhaul loaded");

    // Load settings from INI
    Settings::GetSingleton()->LoadINI();

    auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

    return true;
}