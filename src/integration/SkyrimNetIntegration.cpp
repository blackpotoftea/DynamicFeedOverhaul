#include "PCH.h"
#include "SkyrimNetIntegration.h"
#include "VampireIntegrationUtils.h"  // EmptyCallback for VM dispatch

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
// windows.h defines macros that conflict with CommonLibSSE
#ifdef GetObject
#undef GetObject
#endif
#ifdef SendMessage
#undef SendMessage
#endif

namespace SkyrimNetIntegration {

    static bool g_checked = false;
    static bool g_available = false;

    bool Initialize() {
        if (g_checked) return g_available;
        g_checked = true;

        // SkyrimNet is an SKSE plugin already loaded by the runtime; use
        // GetModuleHandle rather than LoadLibrary so we don't take an extra
        // refcount or trigger a fresh load (matches VampireFeedProxyIntegration).
        HMODULE hDLL = GetModuleHandleA("SkyrimNet.dll");
        if (!hDLL) {
            SKSE::log::info("SkyrimNet DLL not found - integration disabled");
            return false;
        }

        GetVersion = reinterpret_cast<int(*)()>(
            GetProcAddress(hDLL, "PublicGetVersion"));

        if (!GetVersion) {
            SKSE::log::warn("SkyrimNet found but PublicGetVersion not exported - old version?");
            return false;
        }

        int version = GetVersion();
        SKSE::log::info("SkyrimNet API v{} detected", version);

        if (version < 3) {
            SKSE::log::warn("SkyrimNet API v3+ required (found v{})", version);
            return false;
        }

        // v3+ function pointers - Bio Template
        GetBioTemplateName = reinterpret_cast<std::string(*)(uint32_t)>(
            GetProcAddress(hDLL, "PublicGetBioTemplateName"));

        // v3+ function pointers - Data API
        GetMemoriesForActor = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
            GetProcAddress(hDLL, "PublicGetMemoriesForActor"));

        GetRecentEvents = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
            GetProcAddress(hDLL, "PublicGetRecentEvents"));

        GetRecentDialogue = reinterpret_cast<std::string(*)(uint32_t, int)>(
            GetProcAddress(hDLL, "PublicGetRecentDialogue"));

        GetLatestDialogueInfo = reinterpret_cast<std::string(*)()>(
            GetProcAddress(hDLL, "PublicGetLatestDialogueInfo"));

        IsMemorySystemReady = reinterpret_cast<bool(*)()>(
            GetProcAddress(hDLL, "PublicIsMemorySystemReady"));

        GetActorEngagement = reinterpret_cast<std::string(*)(int, bool, bool, double, double)>(
            GetProcAddress(hDLL, "PublicGetActorEngagement"));

        GetRelatedActors = reinterpret_cast<std::string(*)(uint32_t, int, double, double)>(
            GetProcAddress(hDLL, "PublicGetRelatedActors"));

        GetPlayerContext = reinterpret_cast<std::string(*)(float)>(
            GetProcAddress(hDLL, "PublicGetPlayerContext"));

        GetEventPairCounts = reinterpret_cast<std::string(*)(const char*, int)>(
            GetProcAddress(hDLL, "PublicGetEventPairCounts"));

        // Plugin config API
        GetPluginConfig = reinterpret_cast<std::string(*)(const char*)>(
            GetProcAddress(hDLL, "PublicGetPluginConfig"));

        GetPluginConfigValue = reinterpret_cast<std::string(*)(const char*, const char*, const char*)>(
            GetProcAddress(hDLL, "PublicGetPluginConfigValue"));

        // Decorator API (v3.1+)
        RegisterDecorator = reinterpret_cast<bool(*)(const char*, const char*,
            std::function<std::string(RE::Actor*)>)>(
            GetProcAddress(hDLL, "PublicRegisterDecorator"));

        HasDecorator = reinterpret_cast<bool(*)(const char*)>(
            GetProcAddress(hDLL, "PublicHasDecorator"));

        // Actor Busy API (v3.1+)
        SetActorBusy = reinterpret_cast<bool(*)(uint32_t, const char*)>(
            GetProcAddress(hDLL, "PublicSetActorBusy"));

        ClearActorBusy = reinterpret_cast<bool(*)(uint32_t)>(
            GetProcAddress(hDLL, "PublicClearActorBusy"));

        IsActorBusy = reinterpret_cast<bool(*)(uint32_t)>(
            GetProcAddress(hDLL, "PublicIsActorBusy"));

        // Event callback API (v3.1+)
        RegisterEventCallback = reinterpret_cast<uint64_t(*)(const char*, std::function<void(const char*)>)>(
            GetProcAddress(hDLL, "PublicRegisterEventCallback"));

        UnregisterEventCallback = reinterpret_cast<bool(*)(uint64_t)>(
            GetProcAddress(hDLL, "PublicUnregisterEventCallback"));

        // World knowledge API - global (v7+) and per-actor (v9+).
        // Null on older SkyrimNet builds; callers must null-check.
        GetWorldKnowledge = reinterpret_cast<std::string(*)(int)>(
            GetProcAddress(hDLL, "PublicGetWorldKnowledge"));
        GetWorldKnowledgeForActor = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
            GetProcAddress(hDLL, "PublicGetWorldKnowledgeForActor"));

        SKSE::log::info("SkyrimNet Data API: Memories={}, Events={}, Dialogue={}, LatestDialogue={}, Ready={}, "
                        "ActorEngagement={}, RelatedActors={}, PlayerContext={}, EventPairs={}, "
                        "EventCallback={}, BioTemplate={}, WorldKnowledge={}",
                        GetMemoriesForActor != nullptr, GetRecentEvents != nullptr,
                        GetRecentDialogue != nullptr, GetLatestDialogueInfo != nullptr,
                        IsMemorySystemReady != nullptr, GetActorEngagement != nullptr,
                        GetRelatedActors != nullptr, GetPlayerContext != nullptr,
                        GetEventPairCounts != nullptr,
                        RegisterEventCallback != nullptr, GetBioTemplateName != nullptr,
                        GetWorldKnowledgeForActor != nullptr);

        g_available = true;
        return true;
    }

    bool IsAvailable() {
        if (!g_checked) Initialize();
        return g_available;
    }

    void Setup() {
        if (!IsAvailable()) {
            SKSE::log::info("SkyrimNet Setup skipped - API not available");
            return;
        }

        // One-time startup registration site for our SkyrimNet hooks. This is where
        // future passes register C++ decorators (RegisterDecorator) that expose
        // vampire-feed state to SkyrimNet's prompt templates / eligibility rules,
        // and/or subscribe to SkyrimNet events (RegisterEventCallback). Each call
        // must null-check its function pointer first.
        //
        // Note: emitting our own events (vampire_feed / vampire_feed_failed) needs no
        // setup here - SkyrimNet event types are free-form and are pushed on demand via
        // RegisterEvent() at feed time.
        SKSE::log::info("SkyrimNet Setup complete - integration hooks registered");
    }

    bool RegisterEvent(const char* eventType, const std::string& text,
        RE::Actor* originator, RE::Actor* target) {
        if (!IsAvailable()) return false;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::warn("SkyrimNet RegisterEvent('{}'): VM not available", eventType);
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
            new VampireIntegrationUtils::EmptyCallback());

        // Papyrus: SkyrimNetApi.RegisterEvent(string eventType, string text, Actor originator, Actor target)
        RE::BSFixedString evType(eventType);
        RE::BSFixedString evText(text.c_str());
        bool ok = vm->DispatchStaticCall(
            "SkyrimNetApi", "RegisterEvent",
            RE::MakeFunctionArguments(std::move(evType), std::move(evText),
                std::move(originator), std::move(target)),
            callback);

        if (!ok) {
            SKSE::log::warn("SkyrimNet RegisterEvent('{}') dispatch failed", eventType);
        } else {
            SKSE::log::debug("SkyrimNet RegisterEvent('{}') dispatched", eventType);
        }
        return ok;
    }

}  // namespace SkyrimNetIntegration
