#pragma once

namespace TargetState {

    // Matches Papyrus GetSleepState return values:
    // 0 - Not sleeping
    // 2 - Not sleeping but wants to (going to sleep)
    // 3 - Sleeping
    // 4 - Wants to wake (waking up)
    inline int GetSleepState(RE::Actor* actor) {
        if (!actor) return 0;

        auto actorState = actor->AsActorState();
        if (!actorState) return 0;

        auto state = actorState->GetSitSleepState();
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

    // Matches Papyrus IsInFurnitureState - checks if actor is using any furniture
    inline bool IsInFurnitureState(RE::Actor* actor) {
        if (!actor) return false;

        auto furnHandle = actor->GetOccupiedFurniture();
        return furnHandle.get() != nullptr;
    }

    // Get the furniture reference the actor is using (nullptr if not using furniture)
    inline RE::TESObjectREFR* GetFurnitureReference(RE::Actor* actor) {
        if (!actor) return nullptr;

        auto furnHandle = actor->GetOccupiedFurniture();
        if (auto furnRef = furnHandle.get()) {
            return furnRef.get();
        }
        return nullptr;
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

        auto actorState = actor->AsActorState();
        if (!actorState) return false;

        auto state = actorState->GetSitSleepState();
        return state == RE::SIT_SLEEP_STATE::kNormal;
    }

    // Get raw SIT_SLEEP_STATE
    inline RE::SIT_SLEEP_STATE GetSitSleepState(RE::Actor* actor) {
        if (!actor) return RE::SIT_SLEEP_STATE::kNormal;

        auto actorState = actor->AsActorState();
        if (!actorState) return RE::SIT_SLEEP_STATE::kNormal;

        return actorState->GetSitSleepState();
    }

    // Get detailed furniture type from furniture flags
    enum class FurnitureType {
        kNone,
        kBed,      // kCanSleep
        kChair,    // kCanSit
        kLean,     // kCanLean
        kOther
    };

    inline FurnitureType GetFurnitureType(RE::TESObjectREFR* furnRef) {
        if (!furnRef) return FurnitureType::kNone;

        auto baseObj = furnRef->GetBaseObject();
        if (!baseObj) return FurnitureType::kOther;

        auto furn = baseObj->As<RE::TESFurniture>();
        if (!furn) return FurnitureType::kOther;

        auto flags = furn->furnFlags;

        if (flags.any(RE::TESFurniture::ActiveMarker::kCanSleep)) {
            return FurnitureType::kBed;
        }
        if (flags.any(RE::TESFurniture::ActiveMarker::kCanSit)) {
            return FurnitureType::kChair;
        }
        if (flags.any(RE::TESFurniture::ActiveMarker::kCanLean)) {
            return FurnitureType::kLean;
        }

        return FurnitureType::kOther;
    }

    inline FurnitureType GetActorFurnitureType(RE::Actor* actor) {
        return GetFurnitureType(GetFurnitureReference(actor));
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
}
