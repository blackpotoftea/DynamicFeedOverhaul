#pragma once

// SkyrimNet Integration - C++ client for the SkyrimNet Public API (v9).
//
// Ported from the official CppAPI/PublicAPI.h into this project's conventions:
//  - namespace + split .h/.cpp (implementation registered in the build)
//  - SKSE::log instead of logger::
//  - GetModuleHandleA (SkyrimNet is already SKSE-loaded) instead of LoadLibraryA
//
// Function pointers are resolved in Initialize() with the same version gating the
// official header uses. Pointers introduced in newer API versions are null on older
// SkyrimNet builds, so ALWAYS null-check a pointer before calling it.
//
// ABI: SkyrimNet and this plugin must share MSVC version + dynamic CRT (/MD); all
// CommonLibSSE-NG plugins satisfy this. Data-query pointers are thread-safe and
// return empty results until a save is loaded (see IsMemorySystemReady).

#include <cstdint>
#include <functional>
#include <string>

namespace RE { class Actor; }

namespace SkyrimNetIntegration {

    // ---- Core (v2+) ----

    /** Runtime API version (9 at time of writing). */
    inline int (*GetVersion)() = nullptr;

    // ---- Actions (v2+) ----

    /** Register a custom LLM action NPCs can perform. Callbacks must be thread-safe. */
    inline bool (*RegisterCPPAction)(const std::string name, const std::string description,
        std::function<bool(RE::Actor*)> eligibleCallback,
        std::function<bool(RE::Actor*, std::string json_params)> executeCallback,
        const std::string triggeringEventTypesCSV, std::string categoryStr, int priority,
        std::string parameterSchemaJSON, std::string customCategory,
        std::string customParentCategory, std::string tagsCSV) = nullptr;

    /** Register an action subcategory (organizational grouping in the action tree). */
    inline bool (*RegisterCPPSubCategory)(const std::string name, const std::string description,
        std::function<bool(RE::Actor*)> eligibleCallback, const std::string triggeringEventTypesCSV,
        int priority, std::string parameterSchemaJSON, std::string customCategory,
        std::string customParentCategory, std::string tagsCSV) = nullptr;

    // ---- UUID resolution (v3+) ----

    /** FormID -> SkyrimNet internal UUID (0 if unknown). */
    inline uint64_t (*FormIDToUUID)(uint32_t formId) = nullptr;
    /** UUID -> FormID (0 if unknown). */
    inline uint32_t (*UUIDToFormID)(uint64_t uuid) = nullptr;
    /** Actor display name for a UUID ("" if unknown). */
    inline std::string (*GetActorNameByUUID)(uint64_t uuid) = nullptr;

    // ---- Bio template (v3+) ----

    /** Bio template name assigned to an actor ("" if none). */
    inline std::string (*GetBioTemplateName)(uint32_t formId) = nullptr;

    // ---- Data queries (v3+, thread-safe; arrays return "[]" on error) ----

    /** Memories for an actor. Non-empty contextQuery => semantic search. */
    inline std::string (*GetMemoriesForActor)(uint32_t formId, int maxCount, const char* contextQuery) = nullptr;
    /** Recent world events. formId 0 = all; eventTypeFilter is CSV or "" for all. */
    inline std::string (*GetRecentEvents)(uint32_t formId, int maxCount, const char* eventTypeFilter) = nullptr;
    /** Recent player<->NPC dialogue, chronological. */
    inline std::string (*GetRecentDialogue)(uint32_t formId, int maxExchanges) = nullptr;
    /** Info about the most recent NPC who spoke to the player. */
    inline std::string (*GetLatestDialogueInfo)() = nullptr;
    /** True once the memory/database is ready (after a save loads). */
    inline bool (*IsMemorySystemReady)() = nullptr;
    /** Per-actor engagement stats for scoring. Windows are in game-seconds. */
    inline std::string (*GetActorEngagement)(int maxCount, bool excludePlayer, bool playerEventsOnly, double shortWindowSeconds, double mediumWindowSeconds) = nullptr;
    /** Actors related to an anchor actor via shared event history. */
    inline std::string (*GetRelatedActors)(uint32_t formId, int maxCount, double shortWindowSeconds, double mediumWindowSeconds) = nullptr;
    /** Comprehensive player context: time, recent interactions, relationships. */
    inline std::string (*GetPlayerContext)(float withinGameHours) = nullptr;
    /** NPC-to-NPC event pair counts within a candidate pool (CSV of FormIDs). */
    inline std::string (*GetEventPairCounts)(const char* formIdListCSV, int minSharedEvents) = nullptr;

    // ---- Diary queries (v4+) ----

    /** Diary entries for an actor, optionally time-filtered (epoch seconds; 0 = unbounded). */
    inline std::string (*GetDiaryEntries)(uint32_t formId, int maxCount, double startTime, double endTime) = nullptr;

    // ---- Memory creation (v5+) ----

    /** Create a per-actor memory (embedded for semantic search). Returns memory id (>0) or 0.
     *  memoryType: EXPERIENCE/RELATIONSHIP/KNOWLEDGE/LOCATION/SKILL/TRAUMA/JOY. */
    inline int (*AddMemory)(uint32_t formId, const char* contentText, float importance,
        const char* memoryType, const char* emotion, const char* location,
        const char* tagsJSON, const char* relatedActorsJSON) = nullptr;

    // ---- Plugin configuration (v3+) ----

    /** Full JSON config for a registered plugin ("{}" if not found). */
    inline std::string (*GetPluginConfig)(const char* pluginName) = nullptr;
    /** Single config value by dot-path, or defaultValue if missing. */
    inline std::string (*GetPluginConfigValue)(const char* pluginName, const char* path, const char* defaultValue) = nullptr;

    // ---- Decorator registration (v5+) ----

    /** Register an Inja/eligibility decorator. Callback receives RE::Actor*, returns a string.
     *  Must be thread-safe; return "" for invalid actors. */
    inline bool (*RegisterDecorator)(const char* name, const char* description,
        std::function<std::string(RE::Actor*)> callback) = nullptr;
    /** True if a decorator with this name exists (built-in or external). */
    inline bool (*HasDecorator)(const char* name) = nullptr;

    // ---- Event callbacks (v5+) ----

    /** Subscribe to an event type (e.g. "dialogue","combat","death"). Callback runs on
     *  SkyrimNet's ThreadPool - do NOT call RE:: from it; copy the const char*. Returns id (>0). */
    inline uint64_t (*RegisterEventCallback)(const char* eventType, std::function<void(const char*)> callback) = nullptr;
    /** Unregister a callback by the id RegisterEventCallback returned. */
    inline bool (*UnregisterEventCallback)(uint64_t callbackId) = nullptr;

    // ---- Actor busy state (v6+) ----

    /** Mark an actor busy (is_busy()/busy_reason() decorators). Caller must clear it later. */
    inline bool (*SetActorBusy)(uint32_t formId, const char* reason) = nullptr;
    /** Clear an actor's busy state (on completion/failure/interruption). */
    inline bool (*ClearActorBusy)(uint32_t formId) = nullptr;
    /** True if the actor is currently busy. */
    inline bool (*IsActorBusy)(uint32_t formId) = nullptr;

    // ---- Current save UUID (v7+) ----

    /** Unique id for the current save ("" if no save loaded). */
    inline std::string (*GetSaveUniqueID)() = nullptr;

    // ---- World knowledge (v7+ global CRUD, v9+ per-actor) ----

    /** Create a shared world-knowledge entry gated by an Inja conditionExpr. Returns id (>0) or 0. */
    inline int (*AddWorldKnowledge)(const char* content, const char* conditionExpr,
        bool alwaysInject, float importance, const char* displayName) = nullptr;
    /** Remove a world-knowledge entry by id (refuses per-actor memories). */
    inline bool (*RemoveWorldKnowledge)(int memoryId) = nullptr;
    /** All world-knowledge entries as a JSON array. */
    inline std::string (*GetWorldKnowledge)(int maxCount) = nullptr;
    /** World-knowledge applicable to an actor. Empty searchQuery = cheap always-inject only. */
    inline std::string (*GetWorldKnowledgeForActor)(uint32_t formId, int maxResults, const char* searchQuery) = nullptr;

    // ---- Custom LLM prompts (v8+) ----

    /** Render+submit a named prompt template to the LLM; callback fires on a ThreadPool
     *  worker (thread-safe; do NOT call RE:: from it). Returns false on immediate error. */
    inline bool (*SendCustomPromptToLLM)(const char* promptName, const char* variant, const char* contextJson,
        std::function<void(const char* response, int success)> callback) = nullptr;

    // ---- Lifecycle / integration ----

    /**
     * Detect SkyrimNet and resolve its exported function pointers (version-gated).
     * Returns true if SkyrimNet.dll is loaded and PublicGetVersion resolved.
     * Safe to call when SkyrimNet is absent (returns false). Idempotent.
     */
    bool Initialize();

    /** True if SkyrimNet was detected and its API loaded. Lazily initializes if needed. */
    bool IsAvailable();

    /**
     * Register our hooks into SkyrimNet (C++ decorators, actions, event callbacks).
     * Call once at startup after a successful Initialize(), gated on the
     * Integration.EnableSkyrimNet setting. No-op if SkyrimNet is unavailable.
     */
    void Setup();

    /**
     * Emit an event into SkyrimNet's memory/event log from native code.
     * SkyrimNet exposes no native event-emit export (the API only lets you subscribe
     * via RegisterEventCallback), so this dispatches the Papyrus static
     * SkyrimNetApi.RegisterEvent through the VM - no .psc scripts needed in this plugin.
     *
     * eventType is a free-form string (e.g. "vampire_feed", "vampire_feed_failed") -
     * SkyrimNet does not require event types to be pre-registered. text is the
     * natural-language description handed to the LLM. originator/target may be null.
     * Returns false if SkyrimNet is unavailable or the VM dispatch fails.
     */
    bool RegisterEvent(const char* eventType, const std::string& text,
        RE::Actor* originator, RE::Actor* target);

    /**
     * Register a SkyrimNet event TYPE/SCHEMA (fields + Inja format templates) by
     * dispatching the Papyrus static SkyrimNetApi.RegisterEventSchema through the VM -
     * no .psc needed in this plugin. Event types are defined once at startup; SkyrimNet
     * treats re-registration of the same type as an upsert.
     *
     * fieldsJson / formatTemplatesJson are SkyrimNet's schema JSON blobs (field "type":
     * 0=string, 2=bool). defaultTTLMs is the event lifetime; isEphemeral marks
     * non-persistent events. Returns false if SkyrimNet is unavailable or dispatch fails.
     * Our feed schemas (vampire_feed / vampire_feed_failed) are registered from Setup().
     */
    bool RegisterEventSchema(const char* eventType, const char* displayName, const char* description,
        const char* fieldsJson, const char* formatTemplatesJson, bool isEphemeral, int defaultTTLMs,
        bool shortLivedEnabled = true, bool interrupt = false);

    /**
     * Emit a "vampire_feed" event for a completed feed. Classifies feed_type
     * (combat / stealth_sleeping / stealth / willing / normal) from detection + posture,
     * builds the schema JSON, and routes through RegisterEvent (non-combat, persistent)
     * or the short-lived event path (in combat, ttl-bounded). No-op if SkyrimNet is
     * unavailable or either actor is null. ttl is in milliseconds (combat path only).
     * killed=true marks a feed that drained the victim dry (composite drain-kill, or a
     * lethal legacy feed) - surfaced as the schema's "killed" field.
     */
    void RegisterVampireFeedEvent(RE::Actor* attacker, RE::Actor* target, bool killed = false, int ttl = 120);

    /**
     * Emit a "vampire_feed_failed" event for an aborted/failed feed attempt.
     * failureReason is a free-form tag (e.g. "animation_failed", "witnessed"). Same
     * combat routing as RegisterVampireFeedEvent. No-op if either actor is null.
     */
    void RegisterVampireFeedFailedEvent(RE::Actor* attacker, RE::Actor* target,
        const char* failureReason = "animation_failed", int ttl = 120);

}  // namespace SkyrimNetIntegration
