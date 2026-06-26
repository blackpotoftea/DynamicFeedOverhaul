#pragma once

// SkyrimNet Integration - C++ API client for SkyrimNet, an LLM-driven NPC mod.
//
// Provides function pointers to SkyrimNet's public API for:
// - Decorator registration (C++ decorators for synchronous eligibility checks)
// - Actor busy state (block SkyrimNet actions while an actor is occupied)
// - Data API (memories, events, dialogue, analytics)
// - World knowledge
//
// Usage:
//   Call SkyrimNetIntegration::Initialize() after kDataLoaded.
//   Check IsAvailable() / individual function pointers for null before calling.
//
// Based on SkyrimNet Public API v3. The full API surface is ported here even where
// unused today so future behavior work has it ready.

#include <cstdint>
#include <functional>
#include <string>

namespace RE { class Actor; }

namespace SkyrimNetIntegration {

    // ---- Core ----
    inline int (*GetVersion)() = nullptr;

    // ---- Bio Template (v3+) ----

    /** Get the bio template name for an actor (used for bio prompt file lookup). */
    inline std::string (*GetBioTemplateName)(uint32_t formId) = nullptr;

    // ---- Data API (v3+) ----

    /** Retrieve memories for an actor. contextQuery enables semantic search if non-empty. */
    inline std::string (*GetMemoriesForActor)(uint32_t formId, int maxCount, const char* contextQuery) = nullptr;

    /** Retrieve recent events, optionally filtered by actor and event type. */
    inline std::string (*GetRecentEvents)(uint32_t formId, int maxCount, const char* eventTypeFilter) = nullptr;

    /** Retrieve recent dialogue between the player and a specific NPC. */
    inline std::string (*GetRecentDialogue)(uint32_t formId, int maxExchanges) = nullptr;

    /** Get the most recent NPC who spoke to the player. */
    inline std::string (*GetLatestDialogueInfo)() = nullptr;

    /** Check if the memory/database system is ready. */
    inline bool (*IsMemorySystemReady)() = nullptr;

    /** Get per-actor engagement statistics (memory + event activity) for caller-side scoring.
     *  shortWindowSeconds/mediumWindowSeconds define recency buckets (e.g., 86400=24h, 604800=7d). */
    inline std::string (*GetActorEngagement)(int maxCount, bool excludePlayer, bool playerEventsOnly, double shortWindowSeconds, double mediumWindowSeconds) = nullptr;

    /** Get actors related to a given actor via shared event history.
     *  shortWindowSeconds/mediumWindowSeconds define recency buckets (e.g., 86400=24h, 604800=7d). */
    inline std::string (*GetRelatedActors)(uint32_t formId, int maxCount, double shortWindowSeconds, double mediumWindowSeconds) = nullptr;

    /** Get comprehensive player context: DB time, recent interactions, relationships. */
    inline std::string (*GetPlayerContext)(float withinGameHours) = nullptr;

    /** Get NPC-to-NPC event pair counts within a candidate pool. */
    inline std::string (*GetEventPairCounts)(const char* formIdListCSV, int minSharedEvents) = nullptr;

    // ---- Plugin Configuration API ----

    /** Get the full JSON config for a registered plugin. */
    inline std::string (*GetPluginConfig)(const char* pluginName) = nullptr;

    /** Get a single string config value by dot-path from a plugin's settings. */
    inline std::string (*GetPluginConfigValue)(const char* pluginName, const char* path, const char* defaultValue) = nullptr;

    // ---- Decorator API (v3.1+) ----

    /** Register a native C++ decorator. Callback receives RE::Actor*, returns string for Inja templates.
     *  Used in eligibilityRules (synchronous, no cache delay) and prompt templates. */
    inline bool (*RegisterDecorator)(const char* name, const char* description,
        std::function<std::string(RE::Actor*)> callback) = nullptr;

    /** Check if a decorator with this name already exists. */
    inline bool (*HasDecorator)(const char* name) = nullptr;

    // ---- Actor Busy API (v3.1+) ----

    /** Mark an actor as busy with a reason string. Blocks actions with is_busy eligibility check. */
    inline bool (*SetActorBusy)(uint32_t formId, const char* reason) = nullptr;

    /** Clear busy state for an actor. */
    inline bool (*ClearActorBusy)(uint32_t formId) = nullptr;

    /** Check if an actor is busy. */
    inline bool (*IsActorBusy)(uint32_t formId) = nullptr;

    // ---- Event Callback API (v3.1+) ----

    /** Register a callback for a specific event type (e.g., "dialogue"). Thread-safe. */
    inline uint64_t (*RegisterEventCallback)(const char* eventType, std::function<void(const char*)> callback) = nullptr;

    /** Unregister a previously registered event callback by ID. */
    inline bool (*UnregisterEventCallback)(uint64_t callbackId) = nullptr;

    // ---- World Knowledge API (v7+ for global, v9+ for per-actor) ----

    /** Get all world knowledge entries as a JSON array.
     *  Each entry includes id, content, condition_expr, always_inject, importance,
     *  display_name, is_active. Returns "[]" on error. v7+. */
    inline std::string (*GetWorldKnowledge)(int maxCount) = nullptr;

    /** Get world knowledge entries applicable to an actor as a JSON array.
     *  Empty searchQuery returns deterministic always-inject entries only (cheap, no HNSW).
     *  Non-empty searchQuery enables semantic search. Returns "[]" on error or if SkyrimNet older than v9. */
    inline std::string (*GetWorldKnowledgeForActor)(uint32_t formId, int maxResults, const char* searchQuery) = nullptr;

    /**
     * Initialize the SkyrimNet API by loading function pointers from the DLL.
     * Returns true if SkyrimNet was found and API version is >= 3.
     * Safe to call even if SkyrimNet is not installed (returns false).
     * Idempotent: subsequent calls return the cached result.
     */
    bool Initialize();

    /** True if SkyrimNet was detected and its API loaded. Lazily initializes if needed. */
    bool IsAvailable();

    /**
     * Register our hooks into SkyrimNet (C++ decorators, event callbacks).
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

}  // namespace SkyrimNetIntegration
