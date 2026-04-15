#include "UI.h"
#include "../Settings.h"
#include "../feed/TargetState.h"
#include "../papyrus/PapyrusCall.h"
#include "VampireIntegrationUtils.h"

void UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        return;
    }
    SKSEMenuFramework::SetSection("Dynamic Feed Overhaul");
    SKSEMenuFramework::AddSectionItem("Debug", Debug::Render);
}

void __stdcall UI::Debug::Render() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        ImGuiMCP::Text("Player not available");
        return;
    }

    // Player Status Section
    ImGuiMCP::Text("Player Status");
    ImGuiMCP::Separator();

    // Show race
    auto* race = player->GetRace();
    if (race) {
        ImGuiMCP::Text("Race: %s", race->GetFullName());
    }

    // Show PlayerIsVampire global
    auto* playerIsVampireGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("PlayerIsVampire");
    if (playerIsVampireGlobal) {
        ImGuiMCP::Text("PlayerIsVampire: %.0f", playerIsVampireGlobal->value);
    }

    bool isVampire = TargetState::IsVampire(player);
    bool isWerewolf = TargetState::IsWerewolf(player);
    bool isVampireLord = TargetState::IsVampireLord(player);

    ImGuiMCP::Text("Status: ");
    ImGuiMCP::SameLine();
    if (isVampireLord) {
        ImGuiMCP::TextColored(ImGuiMCP::ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Vampire Lord");
    } else if (isVampire) {
        ImGuiMCP::TextColored(ImGuiMCP::ImVec4(0.8f, 0.0f, 0.2f, 1.0f), "Vampire");
    } else if (isWerewolf) {
        ImGuiMCP::TextColored(ImGuiMCP::ImVec4(0.6f, 0.4f, 0.2f, 1.0f), "Werewolf");
    } else {
        ImGuiMCP::Text("Normal");
    }

    // Show hunger stage for vampires
    if (isVampire && !isVampireLord) {
        int hungerStage = PapyrusCall::GetVampireStage();
        if (hungerStage >= 1 && hungerStage <= 4) {
            const char* hungerNames[] = {"Sated", "Peckish", "Hungry", "Starving"};
            ImGuiMCP::Text("Hunger: %s (Stage %d)", hungerNames[hungerStage - 1], hungerStage);
        }
    }

    // Debug Transformations Section
    ImGuiMCP::Separator();
    ImGuiMCP::Text("Debug Transformations");

    if (ImGuiMCP::Button("Become Vampire")) {
        auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("PlayerVampireQuest");
        if (quest) {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (vm) {
                auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
                if (handle != vm->GetObjectHandlePolicy()->EmptyHandle()) {
                    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                        new VampireIntegrationUtils::EmptyCallback());
                    RE::Actor* playerActor = player;
                    vm->DispatchMethodCall(handle, "PlayerVampireQuestScript", "VampireChange",
                        RE::MakeFunctionArguments(std::move(playerActor)), callback);
                    SKSE::log::info("UI: Called PlayerVampireQuestScript.VampireChange(player)");
                }
            }
        } else {
            SKSE::log::warn("UI: PlayerVampireQuest not found");
        }
    }
    if (ImGuiMCP::Button("Become Vampire Lord")) {
        // TODO: Implement vampire lord transformation
        SKSE::log::info("UI: Become Vampire Lord button pressed");
    }
    if (ImGuiMCP::Button("Become Werewolf")) {
        // TODO: Implement werewolf transformation
        SKSE::log::info("UI: Become Werewolf button pressed");
    }
}
