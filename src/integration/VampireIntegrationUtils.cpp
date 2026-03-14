#include "PCH.h"
#include "VampireIntegrationUtils.h"

namespace VampireIntegrationUtils {

    void PlaySound(RE::BGSSoundDescriptorForm* sound, RE::Actor* target) {
        if (!sound || !target) return;

        auto* audioManager = RE::BSAudioManager::GetSingleton();
        if (!audioManager) {
            SKSE::log::warn("VampireIntegrationUtils::PlaySound: BSAudioManager not available");
            return;
        }

        RE::BSSoundHandle handle;
        handle.soundID = static_cast<uint32_t>(-1);
        handle.assumeSuccess = false;
        handle.state.reset();

        audioManager->BuildSoundDataFromDescriptor(handle, sound);

        if (handle.IsValid()) {
            handle.SetPosition(target->GetPosition());
            handle.SetObjectToFollow(target->Get3D());
            handle.Play();
            SKSE::log::debug("VampireIntegrationUtils::PlaySound: Playing sound on {}", target->GetName());
        } else {
            SKSE::log::warn("VampireIntegrationUtils::PlaySound: Failed to build sound handle");
        }
    }

    void ShowMessage(RE::BGSMessage* message) {
        if (!message) return;
        RE::BSString result;
        message->GetDescription(result, nullptr);
        RE::DebugNotification(result.c_str());
        SKSE::log::debug("VampireIntegrationUtils::ShowMessage: {}", result.c_str());
    }

    void CastSpell(RE::SpellItem* spell, RE::Actor* caster, RE::Actor* target) {
        if (!spell || !caster) return;
        auto* magicCaster = caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
        if (magicCaster) {
            magicCaster->CastSpellImmediate(spell, false, target, 1.0f, false, 0.0f, nullptr);
            SKSE::log::debug("VampireIntegrationUtils::CastSpell: {} on {}",
                spell->GetName(), target ? target->GetName() : "self");
        }
    }

    void DispelSpell(RE::Actor* actor, RE::SpellItem* spell) {
        if (!actor || !spell) return;
        auto* magicTarget = actor->AsMagicTarget();
        if (magicTarget) {
            RE::ActorHandle handle;
            magicTarget->DispelEffect(spell, handle, nullptr);
            SKSE::log::debug("VampireIntegrationUtils::DispelSpell: {} from {}", spell->GetName(), actor->GetName());
        }
    }

    bool HasMagicEffect(RE::Actor* actor, RE::EffectSetting* effect) {
        if (!actor || !effect) return false;
        auto* magicTarget = actor->AsMagicTarget();
        if (!magicTarget) return false;

        auto* activeEffects = magicTarget->GetActiveEffectList();
        if (!activeEffects) return false;

        for (auto* activeEffect : *activeEffects) {
            if (activeEffect && activeEffect->GetBaseObject() == effect) {
                return true;
            }
        }
        return false;
    }

    bool CallPapyrusMethod(RE::TESQuest* quest, const char* scriptName, const char* funcName) {
        if (!quest) return false;
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return false;

        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

        auto* args = RE::MakeFunctionArguments();

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

        bool result = vm->DispatchMethodCall(handle, scriptName, funcName, args, callback);
        if (!result) delete args;
        return result;
    }

    bool CallPlayerBitesMe(RE::TESQuest* dlc1VampireTurnQuest, RE::Actor* target) {
        if (!target || !dlc1VampireTurnQuest) return false;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return false;

        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
            RE::TESQuest::FORMTYPE, dlc1VampireTurnQuest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) {
            SKSE::log::warn("VampireIntegrationUtils::CallPlayerBitesMe: Failed to get handle for DLC1VampireTurn");
            return false;
        }

        auto* args = RE::MakeFunctionArguments(std::move(target));

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

        bool result = vm->DispatchMethodCall(
            handle,
            "DLC1VampireTurnScript",
            "PlayerBitesMe",
            args,
            callback
        );

        if (!result) {
            delete args;
            SKSE::log::warn("VampireIntegrationUtils::CallPlayerBitesMe: PlayerBitesMe call failed");
        } else {
            SKSE::log::debug("VampireIntegrationUtils::CallPlayerBitesMe: PlayerBitesMe called successfully");
        }

        return result;
    }

    bool SetQuestStage(RE::TESQuest* quest, int stage) {
        if (!quest) return false;
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return false;

        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

        auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(stage));

        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

        bool result = vm->DispatchMethodCall(handle, "Quest", "SetStage", args, callback);
        if (!result) delete args;
        else SKSE::log::info("VampireIntegrationUtils::SetQuestStage: Set {} to stage {}", quest->GetFormEditorID(), stage);
        return result;
    }

    bool GetScriptPropertyInt(RE::TESQuest* quest, const char* scriptName, const char* propertyName, int& outValue) {
        if (!quest) return false;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::warn("VampireIntegrationUtils::GetScriptPropertyInt: VM not available");
            return false;
        }

        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) {
            SKSE::log::warn("VampireIntegrationUtils::GetScriptPropertyInt: Failed to get handle for quest");
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::Object> object;
        if (!vm->FindBoundObject(handle, scriptName, object) || !object) {
            SKSE::log::warn("VampireIntegrationUtils::GetScriptPropertyInt: Script '{}' not found on quest", scriptName);
            return false;
        }

        auto* property = object->GetProperty(propertyName);
        if (!property) {
            SKSE::log::warn("VampireIntegrationUtils::GetScriptPropertyInt: Property '{}' not found", propertyName);
            return false;
        }

        outValue = property->GetSInt();
        SKSE::log::debug("VampireIntegrationUtils::GetScriptPropertyInt: {}.{} = {}", scriptName, propertyName, outValue);
        return true;
    }

    bool SetScriptPropertyInt(RE::TESQuest* quest, const char* scriptName, const char* propertyName, int value) {
        if (!quest) return false;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::warn("VampireIntegrationUtils::SetScriptPropertyInt: VM not available");
            return false;
        }

        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) {
            SKSE::log::warn("VampireIntegrationUtils::SetScriptPropertyInt: Failed to get handle for quest");
            return false;
        }

        RE::BSTSmartPointer<RE::BSScript::Object> object;
        if (!vm->FindBoundObject(handle, scriptName, object) || !object) {
            SKSE::log::warn("VampireIntegrationUtils::SetScriptPropertyInt: Script '{}' not found on quest", scriptName);
            return false;
        }

        auto* property = object->GetProperty(propertyName);
        if (!property) {
            SKSE::log::warn("VampireIntegrationUtils::SetScriptPropertyInt: Property '{}' not found", propertyName);
            return false;
        }

        property->SetSInt(value);
        SKSE::log::debug("VampireIntegrationUtils::SetScriptPropertyInt: {}.{} = {}", scriptName, propertyName, value);
        return true;
    }

}
