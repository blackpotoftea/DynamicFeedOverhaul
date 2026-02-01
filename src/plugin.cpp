#include "log.h"
#include "hook.h"
#include "Settings.h"
#include "VampireFeedSink.h"
#include "SkyPrompt/API.hpp"

SkyPromptAPI::ClientID g_clientID = 0;

void OnDataLoaded()
{
    g_clientID = SkyPromptAPI::RequestClientID();
    if (g_clientID == 0) {
        SKSE::log::error("Failed to obtain SkyPrompt ClientID");
        return;
    }
    SKSE::log::info("Obtained SkyPrompt ClientID: {}", g_clientID);

    Hooks::Install();
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

    SKSE::log::info("SkyPromptVampireFeed loaded");

    // Load settings from INI
    Settings::GetSingleton()->LoadINI();

    auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}

    return true;
}