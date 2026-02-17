#pragma once

#include <RE/Skyrim.h>

namespace TargetState {

    // Get raw SIT_SLEEP_STATE - must be first as other functions depend on it
    inline RE::SIT_SLEEP_STATE GetSitSleepState(RE::Actor* actor) {
        if (!actor) return RE::SIT_SLEEP_STATE::kNormal;

        auto actorState = actor->AsActorState();
        if (!actorState) return RE::SIT_SLEEP_STATE::kNormal;

        return actorState->GetSitSleepState();
    }

    // Matches Papyrus GetSleepState return values:
    // 0 - Not sleeping
    // 2 - Not sleeping but wants to (going to sleep)
    // 3 - Sleeping
    // 4 - Wants to wake (waking up)
    inline int GetSleepState(RE::Actor* actor) {
        if (!actor) return 0;

        auto state = GetSitSleepState(actor);
        switch (state) {
            case RE::SIT_SLEEP_STATE::kWantToSleep:
            case RE::SIT_SLEEP_STATE::kWaitingForSleepAnim:
                return 2;  // Going to sleep
            case RE::SIT_SLEEP_STATE::kIsSleeping:
                return 3;  // Sleeping
            case RE::SIT_SLEEP_STATE::kWantToWake:
                return 4;  // Waking up
            default:
                return 0;  // Not sleeping
        }
    }

    // Get the furniture reference the actor is using (returns empty NiPointer if not using furniture)
    // Note: GetOccupiedFurniture() can return stale data, so also check SitSleepState
    // Returns NiPointer to ensure proper reference counting and lifetime management
    RE::NiPointer<RE::TESObjectREFR> GetFurnitureReference(RE::Actor* actor);

    // Matches Papyrus IsInFurnitureState - checks if actor is using any furniture
    // Note: GetOccupiedFurniture() can return stale data, so also check SitSleepState
    inline bool IsInFurnitureState(RE::Actor* actor) {
        return GetFurnitureReference(actor) != nullptr;
    }

    // Check if actor is sitting (chair, bench, etc.)
    inline bool IsSitting(RE::Actor* actor) {
        if (!actor) return false;

        auto actorState = actor->AsActorState();
        if (!actorState) return false;

        return actorState->IsSitting();
    }

    // Check if actor is sleeping (in bed)
    inline bool IsSleeping(RE::Actor* actor) {
        return GetSleepState(actor) == 3;
    }

    // Check if actor is standing (not in furniture)
    inline bool IsStanding(RE::Actor* actor) {
        if (!actor) return false;

        auto state = GetSitSleepState(actor);
        return state == RE::SIT_SLEEP_STATE::kNormal;
    }

    // Get detailed furniture type from furniture flags
    enum class FurnitureType {
        kNone,
        kBed,      // kCanSleep
        kChair,    // kCanSit
        kLean,     // kCanLean
        kOther
    };

    FurnitureType GetFurnitureType(RE::TESObjectREFR* furnRef);

    inline FurnitureType GetActorFurnitureType(RE::Actor* actor) {
        return GetFurnitureType(GetFurnitureReference(actor).get());
    }

    // String conversions for logging
    inline const char* FurnitureTypeToString(FurnitureType type) {
        switch (type) {
            case FurnitureType::kNone: return "None";
            case FurnitureType::kBed: return "Bed";
            case FurnitureType::kChair: return "Chair";
            case FurnitureType::kLean: return "Lean";
            case FurnitureType::kOther: return "Other";
            default: return "Unknown";
        }
    }

    inline const char* SitSleepStateToString(RE::SIT_SLEEP_STATE state) {
        switch (state) {
            case RE::SIT_SLEEP_STATE::kNormal: return "Normal";
            case RE::SIT_SLEEP_STATE::kWantToSit: return "WantToSit";
            case RE::SIT_SLEEP_STATE::kWaitingForSitAnim: return "WaitingForSitAnim";
            case RE::SIT_SLEEP_STATE::kIsSitting: return "IsSitting";
            case RE::SIT_SLEEP_STATE::kWantToStand: return "WantToStand";
            case RE::SIT_SLEEP_STATE::kWantToSleep: return "WantToSleep";
            case RE::SIT_SLEEP_STATE::kWaitingForSleepAnim: return "WaitingForSleepAnim";
            case RE::SIT_SLEEP_STATE::kIsSleeping: return "IsSleeping";
            case RE::SIT_SLEEP_STATE::kWantToWake: return "WantToWake";
            default: return "Unknown";
        }
    }

    // Get the actor's race
    inline RE::TESRace* GetActorRace(RE::Actor* actor) {
        if (!actor) return nullptr;
        return actor->GetRace();
    }

    // Check if actor is a vampire
    bool IsVampire(RE::Actor* actor);

    // Check if actor is a werewolf
    bool IsWerewolf(RE::Actor* actor);

    // Check if actor is a Vampire Lord
    bool IsVampireLord(RE::Actor* actor);

    // Check if actor is Essential or Protected
    bool IsEssentialOrProtected(RE::Actor* actor);

    // Check if actor is unconscious
    inline bool IsUnconscious(RE::Actor* actor) {
        if (!actor) return false;
        auto actorState = actor->AsActorState();
        if (!actorState) return false;
        return actorState->IsUnconscious();
    }

    // Check if actor is bleeding out
    inline bool IsBleedingOut(RE::Actor* actor) {
        if (!actor) return false;
        auto actorState = actor->AsActorState();
        if (!actorState) return false;
        return actorState->IsBleedingOut();
    }

    // Check if actor is conscious and able to witness (not sleeping, unconscious, or bleeding out)
    inline bool IsConsciousAndAware(RE::Actor* actor) {
        if (!actor) return false;
        return !IsSleeping(actor) && !IsUnconscious(actor) && !IsBleedingOut(actor);
    }
}
