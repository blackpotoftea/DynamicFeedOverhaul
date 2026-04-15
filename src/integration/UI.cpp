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
    SKSEMenuFramework::AddSectionItem("Settings", Settings::Render);
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

void __stdcall UI::Settings::Render() {
    auto* settings = ::Settings::GetSingleton();
    bool changed = false;

    // General Settings
    if (ImGuiMCP::CollapsingHeader("General")) {
        changed |= ImGuiMCP::Checkbox("Enable Mod", &settings->General.EnableMod);
        const char* debugLevels[] = {"Info", "Debug", "Trace"};
        if (ImGuiMCP::Combo("Debug Level", &settings->General.DebugLevel, debugLevels, 3)) {
            changed = true;
        }
        ImGuiMCP::SetItemTooltip("0=Info (default), 1=Debug (moderate), 2=Trace (verbose)");
        changed |= ImGuiMCP::Checkbox("Enable Werewolf", &settings->General.EnableWerewolf);
        ImGuiMCP::SetItemTooltip("Enable feeding for Werewolf form (EXPERIMENTAL)");
        changed |= ImGuiMCP::Checkbox("Enable Vampire Lord", &settings->General.EnableVampireLord);
        changed |= ImGuiMCP::Checkbox("Force Vampire", &settings->General.ForceVampire);
        ImGuiMCP::SetItemTooltip("Debug: Skip vampire check");
        changed |= ImGuiMCP::Checkbox("Check Hunger Stage", &settings->General.CheckHungerStage);
        if (settings->General.CheckHungerStage) {
            changed |= ImGuiMCP::SliderInt("Min Hunger Stage", &settings->General.MinHungerStage, 1, 4);
        }
        changed |= ImGuiMCP::SliderInt("Force Feed Type", &settings->General.ForceFeedType, 0, 10);
        ImGuiMCP::SetItemTooltip("Debug: Force specific FeedType (0=auto)");
        changed |= ImGuiMCP::Checkbox("Debug Animation Cycle", &settings->General.DebugAnimationCycle);
        changed |= ImGuiMCP::SliderFloat("Animation Timeout", &settings->General.AnimationTimeout, 1.0f, 60.0f, "%.1f sec");
        changed |= ImGuiMCP::SliderFloat("Periodic Check Interval", &settings->General.PeriodicCheckInterval, 0.1f, 5.0f, "%.1f sec");
        changed |= ImGuiMCP::SliderFloat("Prompt Delay (Idle)", &settings->General.PromptDelayIdleSeconds, 0.0f, 2.0f, "%.2f sec");
    }

    // Prompt Display Settings
    if (ImGuiMCP::CollapsingHeader("Prompt Display")) {
        changed |= ImGuiMCP::Checkbox("Require Weapon Drawn", &settings->PromptDisplay.RequireWeaponDrawn);
        changed |= ImGuiMCP::Checkbox("Show When Sneaking", &settings->PromptDisplay.ShowWhenSneaking);
        changed |= ImGuiMCP::Checkbox("Require Player Facing", &settings->PromptDisplay.RequirePlayerFacing);
        if (settings->PromptDisplay.RequirePlayerFacing) {
            changed |= ImGuiMCP::SliderFloat("Facing Angle Threshold", &settings->PromptDisplay.FacingAngleThreshold, 15.0f, 180.0f, "%.0f deg");
        }
        changed |= ImGuiMCP::Checkbox("Relaxed Combat Targeting", &settings->PromptDisplay.RelaxedCombatTargeting);
        ImGuiMCP::SetItemTooltip("Disable facing requirement during combat");
        changed |= ImGuiMCP::SliderFloat("Max Target Distance", &settings->PromptDisplay.MaxTargetDistance, 50.0f, 500.0f, "%.0f units");
        changed |= ImGuiMCP::Checkbox("Hide Prompt When Player Dead", &settings->PromptDisplay.HidePromptWhenPlayerDead);
    }

    // Non-Combat Settings
    if (ImGuiMCP::CollapsingHeader("Non-Combat")) {
        changed |= ImGuiMCP::Checkbox("Allow Standing", &settings->NonCombat.AllowStanding);
        changed |= ImGuiMCP::Checkbox("Allow Sleeping", &settings->NonCombat.AllowSleeping);
        changed |= ImGuiMCP::Checkbox("Allow Sitting (Chair)", &settings->NonCombat.AllowSittingChair);
        ImGuiMCP::SetItemTooltip("Excluded by default (no animation)");
        changed |= ImGuiMCP::Checkbox("Enable Height Adjust", &settings->NonCombat.EnableHeightAdjust);
        if (settings->NonCombat.EnableHeightAdjust) {
            changed |= ImGuiMCP::SliderFloat("Min Height Diff", &settings->NonCombat.MinHeightDiff, 0.0f, 50.0f, "%.0f");
            changed |= ImGuiMCP::SliderFloat("Max Height Diff", &settings->NonCombat.MaxHeightDiff, 50.0f, 300.0f, "%.0f");
        }
        changed |= ImGuiMCP::Checkbox("Use Two Single Animations", &settings->NonCombat.UseTwoSingleAnimations);
        changed |= ImGuiMCP::SliderFloat("Target Offset X", &settings->NonCombat.TargetOffsetX, -200.0f, 200.0f, "%.0f");
        changed |= ImGuiMCP::SliderFloat("Target Offset Y", &settings->NonCombat.TargetOffsetY, 0.0f, 200.0f, "%.0f");
        changed |= ImGuiMCP::SliderFloat("Target Offset Z", &settings->NonCombat.TargetOffsetZ, -100.0f, 100.0f, "%.0f");
        ImGuiMCP::Separator();
        changed |= ImGuiMCP::Checkbox("Enable Lethal Feed", &settings->NonCombat.EnableLethalFeed);
        ImGuiMCP::SetItemTooltip("Enable hold-to-kill feature for non-combat targets");
        if (settings->NonCombat.EnableLethalFeed) {
            changed |= ImGuiMCP::SliderFloat("Lethal Hold Duration", &settings->NonCombat.LethalHoldDuration, 1.0f, 15.0f, "%.1f sec");
            changed |= ImGuiMCP::Checkbox("Exclude Essential From Lethal", &settings->NonCombat.ExcludeEssentialFromLethal);
        }
        changed |= ImGuiMCP::Checkbox("Enable Rotation", &settings->NonCombat.EnableRotation);
        changed |= ImGuiMCP::Checkbox("Enable Level Check", &settings->NonCombat.EnableLevelCheck);
        if (settings->NonCombat.EnableLevelCheck) {
            changed |= ImGuiMCP::SliderInt("Max Level Difference", &settings->NonCombat.MaxLevelDifference, 0, 50);
        }
    }

    // Combat Settings
    if (ImGuiMCP::CollapsingHeader("Combat")) {
        changed |= ImGuiMCP::Checkbox("Enabled", &settings->Combat.Enabled);
        changed |= ImGuiMCP::Checkbox("Ignore Hunger Check", &settings->Combat.IgnoreHungerCheck);
        changed |= ImGuiMCP::Checkbox("Require Low Health", &settings->Combat.RequireLowHealth);
        if (settings->Combat.RequireLowHealth) {
            changed |= ImGuiMCP::SliderFloat("Low Health Threshold", &settings->Combat.LowHealthThreshold, 0.05f, 0.75f, "%.0f%%");
        }
        ImGuiMCP::Separator();
        changed |= ImGuiMCP::Checkbox("Allow Staggered", &settings->Combat.AllowStaggered);
        ImGuiMCP::SetItemTooltip("Allow feeding on staggered targets (bypasses health check)");
        if (settings->Combat.AllowStaggered) {
            changed |= ImGuiMCP::Checkbox("Stagger Require Lower Level", &settings->Combat.StaggerRequireLowerLevel);
            if (settings->Combat.StaggerRequireLowerLevel) {
                changed |= ImGuiMCP::SliderInt("Stagger Max Level Diff", &settings->Combat.StaggerMaxLevelDifference, 0, 50);
            }
        }
        ImGuiMCP::Separator();
        changed |= ImGuiMCP::Checkbox("Enable Witness Detection", &settings->Combat.EnableWitnessDetection);
        if (settings->Combat.EnableWitnessDetection) {
            changed |= ImGuiMCP::SliderFloat("Witness Detection Radius", &settings->Combat.WitnessDetectionRadius, 500.0f, 5000.0f, "%.0f units");
            changed |= ImGuiMCP::SliderFloat("Witness Check Interval", &settings->Combat.WitnessCheckInterval, 0.1f, 2.0f, "%.1f sec");
            changed |= ImGuiMCP::Checkbox("Witness Debug Logging", &settings->Combat.WitnessDebugLogging);
        }
        changed |= ImGuiMCP::SliderFloat("Prompt Delay (Combat)", &settings->Combat.PromptDelayCombatSeconds, 0.0f, 2.0f, "%.2f sec");
    }

    // Filtering Settings
    if (ImGuiMCP::CollapsingHeader("Filtering")) {
        changed |= ImGuiMCP::Checkbox("Exclude In Scene", &settings->Filtering.ExcludeInScene);
        ImGuiMCP::SetItemTooltip("Skip actors in dialogues/scripted events");
        changed |= ImGuiMCP::Checkbox("Exclude OStim Scenes", &settings->Filtering.ExcludeOStimScenes);
        changed |= ImGuiMCP::Checkbox("Exclude Dead", &settings->Filtering.ExcludeDead);
        changed |= ImGuiMCP::Checkbox("Allow Recently Dead", &settings->Filtering.AllowRecentlyDead);
        if (settings->Filtering.AllowRecentlyDead) {
            changed |= ImGuiMCP::SliderFloat("Max Dead Hours", &settings->Filtering.MaxDeadHours, 0.5f, 24.0f, "%.1f hrs");
            changed |= ImGuiMCP::SliderInt("Max Dead Feeds", &settings->Filtering.MaxDeadFeeds, 0, 10);
            ImGuiMCP::SetItemTooltip("0 = unlimited");
        }
    }

    // Icon Overlay Settings
    if (ImGuiMCP::CollapsingHeader("Icon Overlay")) {
        changed |= ImGuiMCP::Checkbox("Enable Icon Overlay", &settings->IconOverlay.EnableIconOverlay);
        if (settings->IconOverlay.EnableIconOverlay) {
            const char* positions[] = {"Above Head", "Right of Head"};
            if (ImGuiMCP::Combo("Icon Position", &settings->IconOverlay.IconPosition, positions, 2)) {
                changed = true;
            }
            changed |= ImGuiMCP::SliderFloat("Icon Duration", &settings->IconOverlay.IconDuration, 1.0f, 15.0f, "%.1f sec");
            changed |= ImGuiMCP::SliderFloat("Icon Size", &settings->IconOverlay.IconSize, 16.0f, 128.0f, "%.0f px");
            changed |= ImGuiMCP::SliderFloat("Icon Height Offset", &settings->IconOverlay.IconHeightOffset, 0.0f, 50.0f, "%.0f");
        }
    }

    // Animation Settings
    if (ImGuiMCP::CollapsingHeader("Animation")) {
        changed |= ImGuiMCP::Checkbox("Enable Random Selection", &settings->Animation.EnableRandomSelection);
        changed |= ImGuiMCP::SliderInt("Hungry Threshold", &settings->Animation.HungryThreshold, 1, 4);
        ImGuiMCP::SetItemTooltip("Hunger stage >= this uses hungry animations");
        changed |= ImGuiMCP::Checkbox("Enable Time Slowdown", &settings->Animation.EnableTimeSlowdown);
        if (settings->Animation.EnableTimeSlowdown) {
            changed |= ImGuiMCP::SliderFloat("Time Slowdown Multiplier", &settings->Animation.TimeSlowdownMultiplier, 0.1f, 1.0f, "%.1fx");
        }
    }

    // Integration Settings
    if (ImGuiMCP::CollapsingHeader("Integration")) {
        changed |= ImGuiMCP::Checkbox("Enable Sacrosanct", &settings->Integration.EnableSacrosanct);
        changed |= ImGuiMCP::Checkbox("Enable Sacrilege", &settings->Integration.EnableSacrilege);
        changed |= ImGuiMCP::Checkbox("Enable Better Vampires", &settings->Integration.EnableBetterVampires);
        changed |= ImGuiMCP::Checkbox("Poise Ignores Level Check", &settings->Integration.PoiseIgnoresLevelCheck);
        ImGuiMCP::Separator();
        changed |= ImGuiMCP::Checkbox("Deep Sacrosanct Integration", &settings->Integration.DeepSacrosanctIntegration);
        ImGuiMCP::SetItemTooltip("Use C++ to mimic Sacrosanct ProcessFeed (bypasses Papyrus)");
        changed |= ImGuiMCP::Checkbox("Deep Sacrilege Integration", &settings->Integration.DeepSacrilegeIntegration);
        changed |= ImGuiMCP::Checkbox("Enable Sacrosanct In Combat", &settings->Integration.EnableSacrosanctInCombat);
        changed |= ImGuiMCP::Checkbox("Enable Sacrilege In Combat", &settings->Integration.EnableSacrilegeInCombat);
        changed |= ImGuiMCP::Checkbox("Enable Vampire Feed Proxy", &settings->Integration.EnableVampireFeedProxy);
        ImGuiMCP::SetItemTooltip("Skip vanilla feed events when VampireFeedProxy.dll is detected");
    }

    // Save if any setting changed
    if (changed) {
        settings->SaveINI();
    }
}
