#pragma once

#include "../feed/TargetState.h"

// Forward declaration to avoid including Settings.h
class Settings;

namespace PapyrusCall {

    // Enum for vampire mod integrations
    enum class VampireIntegration {
        Vanilla,        // Vanilla Skyrim vampire system (default)
        Sacrosanct,     // Sacrosanct - Vampires of Skyrim
        BetterVampires  // Better Vampires (or other modded vampire quest)
    };

    // Callback for when the Papyrus call completes
    class EmptyCallback : public RE::BSScript::IStackCallbackFunctor {
    public:
        void operator()(RE::BSScript::Variable) override {}
        bool CanSave() const override { return false; }
        void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
    };

    // Check if a script has a function with specific signature
    // Returns: 0 = not found, 1 = no args, 2 = has Actor arg
    inline int GetVampireFeedSignature(RE::TESQuest* quest) {
        if (!quest) return 0;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return 0;

        // Get handle for the quest
        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
            RE::TESQuest::FORMTYPE, quest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return 0;

        // Try to get the script object
        RE::BSTSmartPointer<RE::BSScript::Object> object;
        if (!vm->FindBoundObject(handle, "PlayerVampireQuestScript", object) || !object) {
            return 0;
        }

        // Get the type info
        auto* typeInfo = object->GetTypeInfo();
        if (!typeInfo) return 0;

        // Look for VampireFeed function
        for (uint32_t i = 0; i < typeInfo->GetNumMemberFuncs(); ++i) {
            auto* func = typeInfo->GetMemberFuncIter()[i].func.get();
            if (!func) continue;

            if (func->GetName() == "VampireFeed") {
                auto paramCount = func->GetParamCount();
                SKSE::log::debug("Found VampireFeed with {} parameters", paramCount);

                if (paramCount == 0) {
                    return 1;  // Vanilla: VampireFeed()
                } else if (paramCount == 1) {
                    return 2;  // Modded: VampireFeed(Actor)
                }
            }
        }

        return 0;
    }

    // Call VampireFeed() - vanilla version (no arguments)
    inline bool CallVampireFeedNoArgs(RE::TESQuest* quest) {
        if (!quest) return false;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return false;

        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
            RE::TESQuest::FORMTYPE, quest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

        auto* args = RE::MakeFunctionArguments();
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

        bool result = vm->DispatchMethodCall(
            handle,
            "PlayerVampireQuestScript",
            "VampireFeed",
            args,
            callback
        );

        // DispatchMethodCall takes ownership only on success; delete on failure
        if (!result) {
            delete args;
        }

        SKSE::log::debug("CallVampireFeedNoArgs returned: {}", result);
        return result;
    }

    // Call VampireFeed(Actor target) - modded version with actor argument
    // MakeFunctionArguments handles Actor* conversion via Variable::Pack() internally
    inline bool CallVampireFeedWithActor(RE::TESQuest* quest, RE::Actor* target) {
        if (!quest || !target) return false;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return false;

        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
            RE::TESQuest::FORMTYPE, quest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

        auto* args = RE::MakeFunctionArguments(std::move(target));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

        bool result = vm->DispatchMethodCall(
            handle,
            "PlayerVampireQuestScript",
            "VampireFeed",
            args,
            callback
        );

        // DispatchMethodCall takes ownership only on success; delete on failure
        if (!result) {
            delete args;
        }

        SKSE::log::debug("CallVampireFeedWithActor({}) returned: {}", target->GetName(), result);
        return result;
    }

    // Get the Sacrosanct FeedManager quest by editor ID
    inline RE::TESQuest* GetSacrosanctFeedManagerQuest() {
        return RE::TESForm::LookupByEditorID<RE::TESQuest>("SCS_FeedManager_Quest");
    }

    // Call Sacrosanct ProcessFeed function with full parameters
    // Function signature: ProcessFeed(Actor akTarget, Bool akIsLethal, Bool akIsSleeping,
    //                                 Bool akIsSneakFeed, Bool akIsParalyzed, Bool akIsCombatFeed, Bool akIsEmbrace)
    inline bool CallSacrosanctProcessFeed(RE::TESQuest* quest, RE::Actor* target,
                                         bool isLethal = false, bool isSleeping = false,
                                         bool isSneakFeed = false, bool isParalyzed = false,
                                         bool isCombatFeed = false, bool isEmbrace = false) {
        if (!quest || !target) return false;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return false;

        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
            RE::TESQuest::FORMTYPE, quest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

        // Create arguments: Actor + 7 booleans
        // Note: Must move all arguments to avoid reference types
        auto* args = RE::MakeFunctionArguments(
            std::move(target),
            std::move(isLethal),
            std::move(isSleeping),
            std::move(isSneakFeed),
            std::move(isParalyzed),
            std::move(isCombatFeed),
            std::move(isEmbrace)
        );
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

        bool result = vm->DispatchMethodCall(
            handle,
            "SCS_FeedManager_Quest",
            "ProcessFeed",
            args,
            callback
        );

        // DispatchMethodCall takes ownership only on success; delete on failure
        if (!result) {
            delete args;
        }

        SKSE::log::debug("CallSacrosanctProcessFeed({}) returned: {}", target->GetName(), result);
        return result;
    }

    // Main function: detects signature and calls appropriately
    inline bool CallVampireFeed(RE::TESQuest* quest, RE::Actor* target, bool isLethal = false) {
        // Check if Sacrosanct is enabled and quest present
        auto* settings = Settings::GetSingleton();
        bool enableSacrosanct = settings && settings->Integration.EnableSacrosanct;
        auto* sacrosanctQuest = GetSacrosanctFeedManagerQuest();
        bool hasSacrosanct = enableSacrosanct && sacrosanctQuest != nullptr;

        if (enableSacrosanct && sacrosanctQuest) {
            SKSE::log::info("Sacrosanct quest detected - using Sacrosanct integration");
        }

        // Call vanilla/Better Vampires path first
        bool success = false;
        int signature = GetVampireFeedSignature(quest);

        switch (signature) {
            case 1:
                SKSE::log::debug("Using vanilla VampireFeed() - no args");
                success = CallVampireFeedNoArgs(quest);
                break;
            case 2:
                SKSE::log::debug("Using modded VampireFeed(Actor) - with target");
                success = CallVampireFeedWithActor(quest, target);
                break;
            default:
                SKSE::log::error("VampireFeed function not found on quest");
                break;
        }

        // Then call Sacrosanct if detected
        if (hasSacrosanct) {
            bool isCombatFeed = target->IsInCombat();
            bool isSleeping = TargetState::IsSleeping(target);

            SKSE::log::debug("Calling Sacrosanct ProcessFeed (combat={}, sleeping={}, lethal={})", isCombatFeed, isSleeping, isLethal);
            bool sacrosanctResult = CallSacrosanctProcessFeed(sacrosanctQuest, target, isLethal, isSleeping, false, false, isCombatFeed, false);
            success = success || sacrosanctResult;
        }

        return success;
    }

    // Get the PlayerVampireQuest by editor ID
    inline RE::TESQuest* GetPlayerVampireQuest() {
        return RE::TESForm::LookupByEditorID<RE::TESQuest>("PlayerVampireQuest");
    }

    // Get player vampire hunger stage via VampireFeedReady global
    // Returns: 0-3 (VampireStatus - 1), or -1 if not found
    inline int GetVampireHungerStage() {
        auto* global = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireFeedReady");
        if (global) {
            return static_cast<int>(global->value);
        }
        return -1;
    }
    
    // Get actual vampire stage (1-4)
    // Returns: 1-4 (matching VampireStatus), or -1 if not found
    inline int GetVampireStage() {
        int feedReady = GetVampireHungerStage();
        if (feedReady >= 0) {
            return feedReady + 1;
        }
        return -1;
    }

    // Detect which vampire integration is currently active
    inline VampireIntegration DetectVampireIntegration() {
        auto* settings = Settings::GetSingleton();

        // Check for Sacrosanct first (highest priority)
        if (settings->Integration.EnableSacrosanct) {
            auto* sacrosanctQuest = GetSacrosanctFeedManagerQuest();
            if (sacrosanctQuest) {
                return VampireIntegration::Sacrosanct;
            }
        }

        // Check for modded vampire quest (Better Vampires, etc.)
        if (settings->Integration.EnableBetterVampires) {
            auto* vampireQuest = GetPlayerVampireQuest();
            if (vampireQuest) {
                int signature = GetVampireFeedSignature(vampireQuest);
                if (signature == 2) {
                    // Has Actor parameter - likely Better Vampires or similar mod
                    return VampireIntegration::BetterVampires;
                }
            }
        }

        // Default to Vanilla
        return VampireIntegration::Vanilla;
    }

    // Send OnVampireFeed event to the target actor
    // This is the same event sent by the vanilla StartVampireFeed function
    // Event signature: OnVampireFeed(Actor akTarget)
    inline bool SendOnVampireFeedEvent(RE::Actor* target) {
        if (!target) {
            SKSE::log::error("SendOnVampireFeedEvent: target is null");
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::error("SendOnVampireFeedEvent: VM is null");
            return false;
        }

        // Get handle for the target actor
        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
            RE::Actor::FORMTYPE, target);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) {
            SKSE::log::error("SendOnVampireFeedEvent: failed to get handle for {}", target->GetName());
            return false;
        }

        // Send the event with target as the argument
        // Note: SendEvent always takes ownership of args (unlike DispatchMethodCall)
        auto* args = RE::MakeFunctionArguments(std::move(target));

        vm->SendEvent(handle, "OnVampireFeed", args);

        SKSE::log::debug("SendOnVampireFeedEvent: sent to {} (FormID: {:X})",
            target->GetName(), target->GetFormID());
        return true;
    }

}  // namespace PapyrusCall
