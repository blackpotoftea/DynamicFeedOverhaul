#include "feed/TargetState.h"
#include "utils/FormUtils.h"
#include "PCH.h"

namespace TargetState {

    // Get the furniture reference the actor is using
    RE::NiPointer<RE::TESObjectREFR> GetFurnitureReference(RE::Actor* actor) {
        if (!actor) return nullptr;

        // First check if actor is actually in a sitting/sleeping state
        auto sitSleepState = GetSitSleepState(actor);
        if (sitSleepState == RE::SIT_SLEEP_STATE::kNormal) {
            return nullptr;  // Actor is standing, not using furniture
        }

        auto furnHandle = actor->GetOccupiedFurniture();
        return furnHandle.get();  // Returns NiPointer with proper refcount
    }

    // Get detailed furniture type from furniture flags
    FurnitureType GetFurnitureType(RE::TESObjectREFR* furnRef) {
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

    // Check if actor is a vampire
    bool IsVampire(RE::Actor* actor) {
        if (!actor) return false;
        bool result = FormUtils::HasKeyword(actor, 0x000A82BB, "Skyrim.esm");
        SKSE::log::trace("IsVampire: {}={}", actor->GetName(), result);
        return result;
    }

    // Check if actor is a werewolf
    bool IsWerewolf(RE::Actor* actor) {
        return FormUtils::IsRace(actor, 0x000CDD84, "Skyrim.esm");
    }

    // Check if actor is a Vampire Lord
    bool IsVampireLord(RE::Actor* actor) {
        return FormUtils::IsRace(actor, 0x00283A, "Dawnguard.esm");
    }

    // Check if actor is Essential or Protected
    bool IsEssentialOrProtected(RE::Actor* actor) {
        if (!actor) return false;
        return actor->IsEssential() || actor->IsProtected();
    }
}
