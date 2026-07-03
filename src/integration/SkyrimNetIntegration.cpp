#include "PCH.h"
#include "SkyrimNetIntegration.h"
#include "VampireIntegrationUtils.h"  // EmptyCallback for VM dispatch
#include "feed/TargetState.h"          // sleep/sit posture helpers
#include <nlohmann/json.hpp>           // safe event-data JSON building
#include <ctime>                       // std::time for unique event ids

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

    // Resolve a "Public*" export into the matching stripped-name pointer. decltype keeps
    // the cast in lock-step with the header declaration so the two can't drift.
    #define SKYRIMNET_RESOLVE(ptr, exportName) \
        ptr = reinterpret_cast<decltype(ptr)>(GetProcAddress(hDLL, exportName))

    bool Initialize() {
        if (g_checked) return g_available;
        g_checked = true;

        // SkyrimNet is an SKSE plugin already loaded by the runtime; use GetModuleHandle
        // rather than LoadLibrary so we don't take an extra refcount (matches
        // VampireFeedProxyIntegration). The official header uses LoadLibraryA("SkyrimNet").
        HMODULE hDLL = GetModuleHandleA("SkyrimNet.dll");
        if (!hDLL) {
            SKSE::log::info("SkyrimNet DLL not found - integration disabled");
            return false;
        }

        SKYRIMNET_RESOLVE(GetVersion, "PublicGetVersion");
        if (!GetVersion) {
            SKSE::log::warn("SkyrimNet found but PublicGetVersion not exported - old/incompatible build?");
            return false;
        }

        int version = GetVersion();
        SKSE::log::info("SkyrimNet Public API v{} detected", version);

        // v2+ : action registration
        if (version >= 2) {
            SKYRIMNET_RESOLVE(RegisterCPPAction, "PublicRegisterCPPAction");
            SKYRIMNET_RESOLVE(RegisterCPPSubCategory, "PublicRegisterCPPSubCategory");
        }

        // v3+ : UUID resolution, bio template, data queries, plugin config
        if (version >= 3) {
            SKYRIMNET_RESOLVE(FormIDToUUID, "PublicFormIDToUUID");
            SKYRIMNET_RESOLVE(UUIDToFormID, "PublicUUIDToFormID");
            SKYRIMNET_RESOLVE(GetActorNameByUUID, "PublicGetActorNameByUUID");
            SKYRIMNET_RESOLVE(GetBioTemplateName, "PublicGetBioTemplateName");
            SKYRIMNET_RESOLVE(GetMemoriesForActor, "PublicGetMemoriesForActor");
            SKYRIMNET_RESOLVE(GetRecentEvents, "PublicGetRecentEvents");
            SKYRIMNET_RESOLVE(GetRecentDialogue, "PublicGetRecentDialogue");
            SKYRIMNET_RESOLVE(GetLatestDialogueInfo, "PublicGetLatestDialogueInfo");
            SKYRIMNET_RESOLVE(IsMemorySystemReady, "PublicIsMemorySystemReady");
            SKYRIMNET_RESOLVE(GetActorEngagement, "PublicGetActorEngagement");
            SKYRIMNET_RESOLVE(GetRelatedActors, "PublicGetRelatedActors");
            SKYRIMNET_RESOLVE(GetPlayerContext, "PublicGetPlayerContext");
            SKYRIMNET_RESOLVE(GetEventPairCounts, "PublicGetEventPairCounts");
            SKYRIMNET_RESOLVE(GetPluginConfig, "PublicGetPluginConfig");
            SKYRIMNET_RESOLVE(GetPluginConfigValue, "PublicGetPluginConfigValue");
        }

        // v4+ : diary queries
        if (version >= 4) {
            SKYRIMNET_RESOLVE(GetDiaryEntries, "PublicGetDiaryEntries");
        }

        // v5+ : decorator registration, event callbacks, memory creation
        if (version >= 5) {
            SKYRIMNET_RESOLVE(RegisterDecorator, "PublicRegisterDecorator");
            SKYRIMNET_RESOLVE(HasDecorator, "PublicHasDecorator");
            SKYRIMNET_RESOLVE(RegisterEventCallback, "PublicRegisterEventCallback");
            SKYRIMNET_RESOLVE(UnregisterEventCallback, "PublicUnregisterEventCallback");
            SKYRIMNET_RESOLVE(AddMemory, "PublicAddMemory");
        }

        // v6+ : actor busy state
        if (version >= 6) {
            SKYRIMNET_RESOLVE(SetActorBusy, "PublicSetActorBusy");
            SKYRIMNET_RESOLVE(ClearActorBusy, "PublicClearActorBusy");
            SKYRIMNET_RESOLVE(IsActorBusy, "PublicIsActorBusy");
        }

        // v7+ : save unique id, world knowledge CRUD
        if (version >= 7) {
            SKYRIMNET_RESOLVE(GetSaveUniqueID, "PublicGetSaveUniqueID");
            SKYRIMNET_RESOLVE(AddWorldKnowledge, "PublicAddWorldKnowledge");
            SKYRIMNET_RESOLVE(RemoveWorldKnowledge, "PublicRemoveWorldKnowledge");
            SKYRIMNET_RESOLVE(GetWorldKnowledge, "PublicGetWorldKnowledge");
        }

        // v8+ : send custom prompt to LLM
        if (version >= 8) {
            SKYRIMNET_RESOLVE(SendCustomPromptToLLM, "PublicSendCustomPromptToLLM");
        }

        // v9+ : per-actor world knowledge for prompt enrichment
        if (version >= 9) {
            SKYRIMNET_RESOLVE(GetWorldKnowledgeForActor, "PublicGetWorldKnowledgeForActor");
        }

        g_available = true;
        SKSE::log::info("SkyrimNet API resolved: Actions={}, UUID={}, Data={}, Diary={}, AddMemory={}, "
                        "Decorators={}, EventCallbacks={}, ActorBusy={}, WorldKnowledge={}, PerActorWK={}, CustomPrompt={}",
                        RegisterCPPAction != nullptr, FormIDToUUID != nullptr,
                        GetMemoriesForActor != nullptr, GetDiaryEntries != nullptr, AddMemory != nullptr,
                        RegisterDecorator != nullptr, RegisterEventCallback != nullptr, SetActorBusy != nullptr,
                        GetWorldKnowledge != nullptr, GetWorldKnowledgeForActor != nullptr, SendCustomPromptToLLM != nullptr);
        return true;
    }

    #undef SKYRIMNET_RESOLVE

    bool IsAvailable() {
        if (!g_checked) Initialize();
        return g_available;
    }

    // Register the vampire_feed / vampire_feed_failed event schemas with SkyrimNet.
    // Field/format JSON mirrors SkyrimNet's schema format ("type": 0=string, 2=bool).
    // Done once at startup; re-registering the same type is an upsert.
    static void RegisterFeedEventSchemas() {
        static constexpr const char* kFeedFields =
            R"json([{"name":"attacker","type":0,"required":true,"description":"The attacker doing the feeding"},{"name":"target","type":0,"required":true,"description":"The target being fed upon"},{"name":"feed_type","type":0,"required":false,"description":"Type of feed based on context","defaultValue":"normal"},{"name":"was_detected","type":2,"required":false,"description":"Whether the feeding was detected by the target","defaultValue":false},{"name":"in_combat","type":2,"required":false,"description":"Whether the vampire was in combat during feeding","defaultValue":false},{"name":"target_aware","type":2,"required":false,"description":"Whether the target was aware of the attacker","defaultValue":false},{"name":"killed","type":2,"required":false,"description":"Whether the target was drained dry and killed","defaultValue":false}])json";
        static constexpr const char* kFeedFormats =
            R"json({"recent_events":"**{{attacker}}** feeds on {{target}}{{#if in_combat}} during combat{{/if}}{{#if killed}}, draining them dry{{/if}}{{#if was_detected}} (detected!){{/if}} ({{time_desc}})","raw":"{{attacker}} fed on {{target}}{{#if killed}} (killed){{/if}}","compact":"{{attacker}} -> {{target}} ({{feed_type}} feed{{#if killed}}, killed{{/if}})","verbose":"Vampire Feeding: {{attacker}} fed on {{target}} - Type: {{feed_type}}, Detected: {{was_detected}}, Combat: {{in_combat}}, Victim Aware: {{target_aware}}, Killed: {{killed}}"})json";
        RegisterEventSchema("vampire_feed", "Vampire Feeding Event", "A vampire feeding on a victim",
            kFeedFields, kFeedFormats, false, 120000);

        static constexpr const char* kFailFields =
            R"json([{"name":"attacker","type":0,"required":true,"description":"The vampire who attempted to feed"},{"name":"target","type":0,"required":true,"description":"The intended victim"},{"name":"failure_reason","type":0,"required":false,"description":"Why the feeding failed","defaultValue":"animation_failed"},{"name":"was_in_combat","type":2,"required":false,"description":"Whether the vampire was in combat during the attempt","defaultValue":false},{"name":"target_state","type":0,"required":false,"description":"State of target during attempt","defaultValue":"unknown"}])json";
        static constexpr const char* kFailFormats =
            R"json({"recent_events":"**{{attacker}}** attempted to feed on {{target}} but failed ({{failure_reason}}) ({{time_desc}})","raw":"{{attacker}} failed to feed on {{target}}","compact":"{{attacker}} -> {{target}} (failed: {{failure_reason}})","verbose":"Failed Vampire Feed: {{attacker}} attempted to feed on {{target}} - Reason: {{failure_reason}}, Combat: {{was_in_combat}}, Target State: {{target_state}}"})json";
        RegisterEventSchema("vampire_feed_failed", "Failed Vampire Feeding Attempt",
            "A vampire's failed attempt to feed on a victim", kFailFields, kFailFormats, false, 120000);
    }

    void Setup() {
        if (!IsAvailable()) {
            SKSE::log::info("SkyrimNet Setup skipped - API not available");
            return;
        }

        // One-time startup registration site for our SkyrimNet hooks. This is where
        // future passes register C++ decorators (RegisterDecorator) / actions
        // (RegisterCPPAction) that expose vampire-feed state to SkyrimNet's prompt
        // templates and eligibility rules, and/or subscribe to SkyrimNet events
        // (RegisterEventCallback). Each call must null-check its function pointer first.
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

        // Papyrus: SkyrimNetApi.RegisterEvent(string eventType, string content, Actor originator, Actor target)
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

    bool RegisterEventSchema(const char* eventType, const char* displayName, const char* description,
        const char* fieldsJson, const char* formatTemplatesJson, bool isEphemeral, int defaultTTLMs,
        bool shortLivedEnabled, bool interrupt) {
        if (!IsAvailable()) return false;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::warn("SkyrimNet RegisterEventSchema('{}'): VM not available", eventType);
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
            new VampireIntegrationUtils::EmptyCallback());

        // Papyrus: int SkyrimNetApi.RegisterEventSchema(String eventType, String displayName,
        //   String description, String fieldsJson, String formatTemplatesJson, bool isEphemeral,
        //   int defaultTTLMs, bool shortLivedEnabled = true, bool interrupt = false)
        // All nine args are passed explicitly - the VM does not apply Papyrus-source defaults.
        RE::BSFixedString aType(eventType), aName(displayName), aDesc(description),
            aFields(fieldsJson), aFormats(formatTemplatesJson);
        bool eph = isEphemeral, sle = shortLivedEnabled, intr = interrupt;
        std::int32_t ttl = defaultTTLMs;
        bool ok = vm->DispatchStaticCall(
            "SkyrimNetApi", "RegisterEventSchema",
            RE::MakeFunctionArguments(std::move(aType), std::move(aName), std::move(aDesc),
                std::move(aFields), std::move(aFormats), std::move(eph), std::move(ttl),
                std::move(sle), std::move(intr)),
            callback);

        if (!ok) {
            SKSE::log::warn("SkyrimNet RegisterEventSchema('{}') dispatch failed", eventType);
        } else {
            SKSE::log::info("SkyrimNet RegisterEventSchema('{}') dispatched", eventType);
        }
        return ok;
    }

    // ---- Feed event emission (ports the SkyrimNet Papyrus helper functions) ----

    namespace {
        // Papyrus Actor.IsDetectedBy(akObserver): does the observer currently detect the
        // subject? RequestDetectionLevel > 0 means detected to some degree (same engine
        // call WitnessDetection uses).
        bool DetectsActor(RE::Actor* observer, RE::Actor* subject) {
            if (!observer || !subject) return false;
            return observer->RequestDetectionLevel(subject, RE::DETECTION_PRIORITY::kNormal) > 0;
        }

        std::string ActorName(RE::Actor* actor) {
            const char* n = actor ? actor->GetDisplayFullName() : nullptr;
            return n ? n : "";
        }

        // Papyrus: int SkyrimNetApi.RegisterShortLivedEvent(String eventId, String eventType,
        //   String description, String data, int ttlMs, Actor sourceActor, Actor targetActor)
        bool RegisterShortLivedEvent(const char* eventId, const char* eventType, const std::string& description,
            const std::string& dataJson, int ttlMs, RE::Actor* source, RE::Actor* target) {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                SKSE::log::warn("SkyrimNet RegisterShortLivedEvent('{}'): VM not available", eventType);
                return false;
            }
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                new VampireIntegrationUtils::EmptyCallback());
            RE::BSFixedString aId(eventId), aType(eventType), aDesc(description.c_str()), aData(dataJson.c_str());
            std::int32_t ttl = ttlMs;
            return vm->DispatchStaticCall(
                "SkyrimNetApi", "RegisterShortLivedEvent",
                RE::MakeFunctionArguments(std::move(aId), std::move(aType), std::move(aDesc),
                    std::move(aData), std::move(ttl), std::move(source), std::move(target)),
                callback);
        }

        // Ports RegisterEventByContext: short-lived (ttl-bounded) event in combat, else a
        // persistent RegisterEvent. The Papyrus ValidateEventData() gate is intentionally
        // dropped - the VM call is async so its bool result can't gate us, and we build the
        // JSON to the schema ourselves.
        void RegisterEventByContext(const char* eventId, const char* eventType,
            const std::string& description, const std::string& eventDataJson, int ttl,
            RE::Actor* attacker, RE::Actor* target, bool inCombat) {
            if (inCombat) {
                RegisterShortLivedEvent(eventId, eventType, description, eventDataJson, ttl, attacker, target);
            } else {
                RegisterEvent(eventType, eventDataJson, attacker, target);
            }
        }

        // "<formIdPrefix><targetFormId>_<realSeconds>" - unique enough per feed, mirrors
        // the Papyrus "vampirefeed_<formid>_<realtime>" scheme.
        std::string MakeEventId(const char* prefix, RE::Actor* target) {
            return std::string(prefix) + std::to_string(target ? target->GetFormID() : 0) + "_" +
                   std::to_string(static_cast<std::int64_t>(std::time(nullptr)));
        }
    }

    void RegisterVampireFeedEvent(RE::Actor* attacker, RE::Actor* target, bool killed, int ttl) {
        if (!IsAvailable()) return;
        if (!attacker || !target) {
            SKSE::log::warn("RegisterVampireFeedEvent: invalid actors");
            return;
        }

        const bool inCombat = attacker->IsInCombat();
        const bool wasDetected = DetectsActor(target, attacker);   // attacker.IsDetectedBy(target)
        const bool targetAware = DetectsActor(attacker, target);   // target.IsDetectedBy(attacker)

        const std::string attackerName = ActorName(attacker);
        const std::string targetName = ActorName(target);

        // Feed type from context (order matters - first match wins, as in the Papyrus).
        std::string feedType = "normal";
        if (inCombat) {
            feedType = "combat";
        } else if (TargetState::GetSleepState(target) == 3 && !wasDetected) {
            feedType = "stealth_sleeping";
        } else if (!wasDetected && !targetAware) {
            feedType = "stealth";
        } else if (wasDetected || targetAware) {
            feedType = "willing";
        }

        nlohmann::json j;
        j["attacker"] = attackerName;
        j["target"] = targetName;
        j["feed_type"] = feedType;
        j["was_detected"] = wasDetected;
        j["in_combat"] = inCombat;
        j["target_aware"] = targetAware;
        j["killed"] = killed;

        const std::string eventId = MakeEventId("vampirefeed_", target);
        const std::string description = attackerName + (killed ? " drains " : " feeds on ") + targetName +
            (killed ? " dry" : "");
        RegisterEventByContext(eventId.c_str(), "vampire_feed", description, j.dump(), ttl,
            attacker, target, inCombat);
        SKSE::log::debug("SkyrimNet vampire_feed emitted: {} -> {} ({}{})",
            attackerName, targetName, feedType, killed ? ", killed" : "");
    }

    void RegisterVampireFeedFailedEvent(RE::Actor* attacker, RE::Actor* target,
        const char* failureReason, int ttl) {
        if (!IsAvailable()) return;
        if (!attacker || !target) {
            SKSE::log::warn("RegisterVampireFeedFailedEvent: invalid actors");
            return;
        }

        const bool inCombat = attacker->IsInCombat();
        const std::string attackerName = ActorName(attacker);
        const std::string targetName = ActorName(target);

        std::string targetState;
        if (TargetState::GetSleepState(target) >= 3) {
            targetState = "sleeping";
        } else if (target->IsInCombat()) {
            targetState = "in_combat";
        } else if (TargetState::IsSitting(target)) {
            targetState = "sitting";
        } else {
            targetState = "standing";
        }

        nlohmann::json j;
        j["attacker"] = attackerName;
        j["target"] = targetName;
        j["failure_reason"] = failureReason ? failureReason : "animation_failed";
        j["was_in_combat"] = inCombat;
        j["target_state"] = targetState;

        const std::string eventId = MakeEventId("vampirefeed_failed_", target);
        const std::string description = attackerName + " failed to feed on " + targetName;
        RegisterEventByContext(eventId.c_str(), "vampire_feed_failed", description, j.dump(), ttl,
            attacker, target, inCombat);
        SKSE::log::debug("SkyrimNet vampire_feed_failed emitted: {} -> {} ({})",
            attackerName, targetName, j["failure_reason"].get<std::string>());
    }

}  // namespace SkyrimNetIntegration
