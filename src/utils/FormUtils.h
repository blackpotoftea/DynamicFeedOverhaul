#pragma once

#include <RE/Skyrim.h>
#include <string>
#include <unordered_map>

namespace FormUtils {

    // Cached keyword pointers - SAFE because keywords are permanent forms
    namespace Cache {
        inline RE::BGSKeyword* VampireKeyword = nullptr;
        inline RE::TESRace* WerewolfRace = nullptr;
        inline RE::TESRace* VampireLordRace = nullptr;

        // Editor ID keyword cache for filtering system
        inline std::unordered_map<std::string, RE::BGSKeyword*> KeywordsByEditorID;
    }

    // Initialize cached forms - call this ONCE in your plugin Load() or DataLoaded event
    inline void InitializeCache() {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return;

        // Cache vampire keyword (race keyword check)
        Cache::VampireKeyword = dataHandler->LookupForm<RE::BGSKeyword>(0x000A82BB, "Skyrim.esm");

        // Cache werewolf race
        Cache::WerewolfRace = dataHandler->LookupForm<RE::TESRace>(0x000CDD84, "Skyrim.esm");

        // Cache vampire lord race (requires Dawnguard)
        Cache::VampireLordRace = dataHandler->LookupForm<RE::TESRace>(0x00283A, "Dawnguard.esm");

        SKSE::log::info("FormUtils: Cached {} forms",
            (Cache::VampireKeyword ? 1 : 0) +
            (Cache::WerewolfRace ? 1 : 0) +
            (Cache::VampireLordRace ? 1 : 0));
    }

    // Safely lookup a form by FormID and plugin name (on-demand, no caching)
    template <typename T>
    inline T* LookupForm(RE::FormID formID, const std::string& pluginName) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;
        return dataHandler->LookupForm<T>(formID, pluginName);
    }

    // Check if actor has a keyword by FormID and plugin name (uses cache)
    inline bool HasKeyword(RE::Actor* actor, RE::FormID keywordID, const std::string& pluginName) {
        if (!actor) return false;

        // Use cached keyword if it's the vampire keyword
        RE::BGSKeyword* keyword = nullptr;
        if (keywordID == 0x000A82BB && pluginName == "Skyrim.esm") {
            keyword = Cache::VampireKeyword;
        } else {
            keyword = LookupForm<RE::BGSKeyword>(keywordID, pluginName);
        }

        if (!keyword) return false;

        auto race = actor->GetRace();
        if (!race) return false;

        auto keywordForm = race->As<RE::BGSKeywordForm>();
        if (!keywordForm) return false;

        return keywordForm->HasKeyword(keyword);
    }

    // Check if actor has a keyword by editor ID (with caching)
    inline bool HasKeywordByEditorID(RE::Actor* actor, const std::string& keywordEditorID) {
        if (!actor) return false;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return false;

        // Check cache first
        auto it = Cache::KeywordsByEditorID.find(keywordEditorID);
        RE::BGSKeyword* keyword = nullptr;

        if (it != Cache::KeywordsByEditorID.end()) {
            keyword = it->second;
        } else {
            // Not cached - do lookup and cache it
            keyword = dataHandler->LookupForm<RE::BGSKeyword>(RE::FormID(0), keywordEditorID);
            if (!keyword) {
                // Try to find by iterating keywords (slower but works for all keywords)
                for (const auto& kw : dataHandler->GetFormArray<RE::BGSKeyword>()) {
                    if (kw && kw->GetFormEditorID() == keywordEditorID) {
                        keyword = kw;
                        break;
                    }
                }
            }

            // Cache the result (even if nullptr to avoid repeated searches)
            Cache::KeywordsByEditorID[keywordEditorID] = keyword;
        }

        if (!keyword) return false;
        return actor->HasKeyword(keyword);
    }

    // Check if actor's race matches a specific race by FormID and plugin name (uses cache)
    inline bool IsRace(RE::Actor* actor, RE::FormID raceID, const std::string& pluginName) {
        if (!actor) return false;

        // Use cached races if available
        RE::TESRace* targetRace = nullptr;
        if (raceID == 0x000CDD84 && pluginName == "Skyrim.esm") {
            targetRace = Cache::WerewolfRace;
        } else if (raceID == 0x00283A && pluginName == "Dawnguard.esm") {
            targetRace = Cache::VampireLordRace;
        } else {
            targetRace = LookupForm<RE::TESRace>(raceID, pluginName);
        }

        if (!targetRace) return false;

        auto* actorRace = actor->GetRace();
        return actorRace == targetRace;
    }

    // Optional: Clear the keyword cache (useful if mods are loaded/unloaded at runtime)
    inline void ClearCache() {
        Cache::VampireKeyword = nullptr;
        Cache::WerewolfRace = nullptr;
        Cache::VampireLordRace = nullptr;
        Cache::KeywordsByEditorID.clear();
        SKSE::log::info("FormUtils: Cache cleared");
    }
}
