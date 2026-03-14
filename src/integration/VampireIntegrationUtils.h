#pragma once
#include "PCH.h"

namespace VampireIntegrationUtils {

    // Reusable empty callback for Papyrus calls
    class EmptyCallback : public RE::BSScript::IStackCallbackFunctor {
    public:
        void operator()(RE::BSScript::Variable) override {}
        bool CanSave() const override { return false; }
        void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
    };

    // Sound
    void PlaySound(RE::BGSSoundDescriptorForm* sound, RE::Actor* target);

    // UI
    void ShowMessage(RE::BGSMessage* message);

    // Magic
    void CastSpell(RE::SpellItem* spell, RE::Actor* caster, RE::Actor* target);
    void DispelSpell(RE::Actor* actor, RE::SpellItem* spell);
    bool HasMagicEffect(RE::Actor* actor, RE::EffectSetting* effect);

    // Papyrus
    bool CallPapyrusMethod(RE::TESQuest* quest, const char* scriptName, const char* funcName);
    bool CallPlayerBitesMe(RE::TESQuest* dlc1VampireTurnQuest, RE::Actor* target);
    bool SetQuestStage(RE::TESQuest* quest, int stage);

    // Script properties
    bool GetScriptPropertyInt(RE::TESQuest* quest, const char* scriptName, const char* propertyName, int& outValue);
    bool SetScriptPropertyInt(RE::TESQuest* quest, const char* scriptName, const char* propertyName, int value);

}
