#include "utils/log.h"
#include "hooks/hook.h"
#include "Settings.h"
#include "feed/FeedPromptSink.h"
#include "feed/AnimEventSink.h"
#include "SkyPrompt/API.hpp"
#include "SKSEMCP/SKSEMenuFramework.hpp"
#include "feed/FeedIconOverlay.h"
#include "feed/FeedHealthBarOverlay.h"
#include "integration/OStimIntegration.h"
#include "integration/PoiseIntegration.h"
#include "integration/SacrosanctIntegration.h"
#include "integration/SacrilegeIntegration.h"
#include "integration/VampireFeedProxyIntegration.h"
#include "integration/SkyrimNetIntegration.h"
#include "feed/AnimationRegistry.h"
#include "utils/FormUtils.h"
#include "utils/AnimUtil.h"
#include "integration/UI.h"

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

    // VampireFeedProxy.dll detection - if present and enabled, skip vanilla feed events (proxy handles them)
    auto* settings = Settings::GetSingleton();
    if (VampireFeedProxyIntegration::Initialize()) {
        if (settings->Integration.EnableVampireFeedProxy) {
            SKSE::log::info("VampireFeedProxy detected - vanilla feed events will be skipped");
        } else {
            SKSE::log::info("VampireFeedProxy detected but integration disabled - vanilla feed events will be sent");
        }
    } else {
        SKSE::log::info("VampireFeedProxy not detected - vanilla feed events will be sent");
    }

    // SkyrimNet (LLM-driven NPC mod) integration. Gated on the EnableSkyrimNet
    // setting: Initialize() loads the API, Setup() registers our hooks (decorators/
    // event callbacks) once at startup. Feed-time behavior is wired in a later pass.
    if (settings->Integration.EnableSkyrimNet) {
        if (SkyrimNetIntegration::Initialize()) {
            SKSE::log::info("SkyrimNet detected - API loaded");
            SkyrimNetIntegration::Setup();
        } else {
            SKSE::log::info("SkyrimNet not detected - integration disabled");
        }
    } else {
        SKSE::log::info("SkyrimNet integration disabled in settings");
    }

    if (SKSEMenuFramework::IsInstalled()) {
        SKSE::log::info("SKSEMenuFramework detected, registering icon overlay as HUD element");

        SKSEMenuFramework::AddHudElement([]() {
            FeedIconOverlay::GetSingleton()->RenderOverlay();
            FeedHealthBarOverlay::GetSingleton()->RenderOverlay();
        });
        SKSE::log::info("Successfully registered icon + health bar overlay HUD elements with SKSEMenuFramework");
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

    // Register SKSE Menu Framework UI (no-op if framework not installed)
    UI::Register();

    auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

    return true;
}