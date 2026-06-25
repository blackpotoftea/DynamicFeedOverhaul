#include "UI.h"
#include "../Settings.h"
#include "../feed/TargetState.h"
#include "../papyrus/PapyrusCall.h"
#include "../utils/log.h"
#include "../utils/AnimUtil.h"
#include "../utils/SoundUtil.h"
#include "VampireIntegrationUtils.h"
#include <spdlog/spdlog.h>
#include <sstream>

namespace {
    // Helper to join vector<string> into comma-separated string
    std::string JoinStrings(const std::vector<std::string>& list) {
        std::string result;
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) result += ", ";
            result += list[i];
        }
        return result;
    }

    // Helper to split comma-separated string into vector<string>
    std::vector<std::string> SplitStrings(const char* str) {
        std::vector<std::string> result;
        if (!str || strlen(str) == 0) return result;
        std::stringstream ss(str);
        std::string token;
        while (std::getline(ss, token, ',')) {
            size_t start = token.find_first_not_of(" \t");
            size_t end = token.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                result.push_back(token.substr(start, end - start + 1));
            }
        }
        return result;
    }
}

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
        // Log level dropdown (maps to spdlog verbosity, applied immediately)
        static const char* logLevels[] = {"trace", "debug", "info", "warn", "error"};
        int logLevelIdx = 2;  // default: info
        for (int i = 0; i < 5; ++i) {
            if (settings->General.LogLevel == logLevels[i]) { logLevelIdx = i; break; }
        }
        if (ImGuiMCP::Combo("Log Level", &logLevelIdx, logLevels, 5)) {
            settings->General.LogLevel = logLevels[logLevelIdx];
            ApplyLogLevel(ParseLogLevel(settings->General.LogLevel));
            changed = true;
        }
        ImGuiMCP::SetItemTooltip("Log verbosity. 'trace' is the most detailed, 'error' the least.");
        if (ImGuiMCP::Button("Clear Log")) {
            ClearLog();
        }
        ImGuiMCP::SetItemTooltip("Clears the log file (useful for isolating debug output)");
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

    // Input Settings
    if (ImGuiMCP::CollapsingHeader("Input")) {
        ImGuiMCP::TextDisabled("Key codes (DirectInput scan codes). Common keys:");
        ImGuiMCP::TextDisabled("  E=18, R=19, F=33, G=34, H=35");
        ImGuiMCP::TextDisabled("  Gamepad: A=4096, B=8192, X=16384, Y=32768");
        ImGuiMCP::Separator();
        changed |= ImGuiMCP::InputInt("Feed Key (Keyboard)", &settings->Input.FeedKey, 1, 16);
        ImGuiMCP::SetItemTooltip("Primary feed key (default: 34 = G)");
        changed |= ImGuiMCP::InputInt("Feed Key (Gamepad)", &settings->Input.FeedGamepadKey, 4096, 4096);
        ImGuiMCP::SetItemTooltip("Primary feed gamepad button (default: 4096 = A)");
        changed |= ImGuiMCP::InputInt("Secondary Key (Keyboard)", &settings->Input.SecondaryKey, 1, 16);
        ImGuiMCP::SetItemTooltip("Secondary prompt key for Embrace (default: 35 = H)");
        changed |= ImGuiMCP::InputInt("Secondary Key (Gamepad)", &settings->Input.SecondaryGamepadKey, 4096, 4096);
        ImGuiMCP::SetItemTooltip("Secondary prompt gamepad button (default: 8192 = B)");
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
        ImGuiMCP::Separator();
        changed |= ImGuiMCP::Checkbox("Witness Combat Reaction", &settings->Combat.EnableWitnessCombatReaction);
        ImGuiMCP::SetItemTooltip("An awake victim who survives a feed fights back if brave enough (otherwise only the bounty applies)");
        if (settings->Combat.EnableWitnessCombatReaction) {
            changed |= ImGuiMCP::SliderInt("Assault Confidence Threshold", &settings->Combat.AssaultConfidenceThreshold, 0, 4);
            ImGuiMCP::SetItemTooltip("Minimum victim Confidence to fight back: 0=Cowardly, 1=Cautious, 2=Average, 3=Brave, 4=Foolhardy");
        }
        changed |= ImGuiMCP::SliderFloat("Prompt Delay (Combat)", &settings->Combat.PromptDelayCombatSeconds, 0.0f, 2.0f, "%.2f sec");
    }

    // Filtering Settings
    if (ImGuiMCP::CollapsingHeader("Filtering")) {
        // --- Scene Filters ---
        ImGuiMCP::TextDisabled("Scene Filters");
        ImGuiMCP::Separator();
        changed |= ImGuiMCP::Checkbox("Exclude In Scene", &settings->Filtering.ExcludeInScene);
        ImGuiMCP::SetItemTooltip("Skip actors in dialogues/scripted events");
        changed |= ImGuiMCP::Checkbox("Exclude OStim Scenes", &settings->Filtering.ExcludeOStimScenes);

        // --- Dead Targets (Non-Combat only) ---
        ImGuiMCP::Spacing();
        ImGuiMCP::TextDisabled("Dead Targets (Non-Combat only)");
        ImGuiMCP::Separator();
        ImGuiMCP::TextDisabled("Note: dead targets are always excluded while in combat.");
        changed |= ImGuiMCP::Checkbox("Exclude Dead", &settings->Filtering.ExcludeDead);
        ImGuiMCP::SetItemTooltip("Skip dead actors outside combat (overridden by 'Allow Recently Dead' for fresh corpses)");
        changed |= ImGuiMCP::Checkbox("Allow Recently Dead", &settings->Filtering.AllowRecentlyDead);
        ImGuiMCP::SetItemTooltip("Allow feeding on fresh corpses (overrides 'Exclude Dead' when target is within the limits below)");
        if (settings->Filtering.AllowRecentlyDead) {
            changed |= ImGuiMCP::SliderFloat("Max Dead Hours", &settings->Filtering.MaxDeadHours, 0.5f, 24.0f, "%.1f hrs");
            ImGuiMCP::SetItemTooltip("Maximum in-game hours since death to still allow feeding");
            changed |= ImGuiMCP::SliderInt("Max Dead Feeds", &settings->Filtering.MaxDeadFeeds, 0, 10);
            ImGuiMCP::SetItemTooltip("Max times to feed on a single corpse (0 = unlimited)");
        }

        // --- Keyword / Actor Filters ---
        ImGuiMCP::Spacing();
        ImGuiMCP::TextDisabled("Keyword / Actor Filters (comma-separated)");
        ImGuiMCP::Separator();

        static char includeKeywordsBuf[512] = "";
        static char excludeKeywordsBuf[512] = "";
        static char excludeActorsBuf[1024] = "";
        static bool filterBufsInitialized = false;
        if (!filterBufsInitialized) {
            strncpy(includeKeywordsBuf, JoinStrings(settings->Filtering.IncludeKeywords).c_str(), sizeof(includeKeywordsBuf) - 1);
            strncpy(excludeKeywordsBuf, JoinStrings(settings->Filtering.ExcludeKeywords).c_str(), sizeof(excludeKeywordsBuf) - 1);
            strncpy(excludeActorsBuf, JoinStrings(settings->Filtering.ExcludeActorIDs).c_str(), sizeof(excludeActorsBuf) - 1);
            filterBufsInitialized = true;
        }
        if (ImGuiMCP::InputText("Include Keywords", includeKeywordsBuf, sizeof(includeKeywordsBuf))) {
            settings->Filtering.IncludeKeywords = SplitStrings(includeKeywordsBuf);
            changed = true;
        }
        ImGuiMCP::SetItemTooltip("Only feed if target has ANY of these keywords (empty=allow all)");
        if (ImGuiMCP::InputText("Exclude Keywords", excludeKeywordsBuf, sizeof(excludeKeywordsBuf))) {
            settings->Filtering.ExcludeKeywords = SplitStrings(excludeKeywordsBuf);
            changed = true;
        }
        ImGuiMCP::SetItemTooltip("Never feed if target has ANY of these keywords");
        if (ImGuiMCP::InputText("Exclude Actor IDs", excludeActorsBuf, sizeof(excludeActorsBuf))) {
            settings->Filtering.ExcludeActorIDs = SplitStrings(excludeActorsBuf);
            changed = true;
        }
        ImGuiMCP::SetItemTooltip("Never feed on specific NPCs (format: PluginName|0xFormID)");
    }

    // Overlay Settings
    if (ImGuiMCP::CollapsingHeader("Overlay")) {
        ImGuiMCP::TextDisabled("Icon");
        changed |= ImGuiMCP::Checkbox("Enable Icon Overlay", &settings->IconOverlay.EnableIconOverlay);
        if (settings->IconOverlay.EnableIconOverlay) {
            const char* positions[] = {"Above Head", "Right of Head"};
            if (ImGuiMCP::Combo("Icon Position", &settings->IconOverlay.IconPosition, positions, 2)) {
                changed = true;
            }
            changed |= ImGuiMCP::SliderFloat("Icon Duration", &settings->IconOverlay.IconDuration, 1.0f, 15.0f, "%.1f sec");
            changed |= ImGuiMCP::SliderFloat("Icon Size", &settings->IconOverlay.IconSize, 16.0f, 128.0f, "%.0f px");
            changed |= ImGuiMCP::SliderFloat("Icon Height Offset", &settings->IconOverlay.IconHeightOffset, 0.0f, 50.0f, "%.0f");
            static char iconPathBuf[512] = "";
            static bool iconPathInitialized = false;
            if (!iconPathInitialized) {
                strncpy(iconPathBuf, settings->IconOverlay.IconPath.c_str(), sizeof(iconPathBuf) - 1);
                iconPathInitialized = true;
            }
            if (ImGuiMCP::InputText("Icon Path", iconPathBuf, sizeof(iconPathBuf))) {
                settings->IconOverlay.IconPath = iconPathBuf;
                changed = true;
            }
            ImGuiMCP::SetItemTooltip("Path to the icon file (e.g., Data\\Interface\\ImGuiIcons\\Icons\\vampireFang.png)");

            static char failureIconPathBuf[512] = "";
            static bool failureIconPathInitialized = false;
            if (!failureIconPathInitialized) {
                strncpy(failureIconPathBuf, settings->IconOverlay.FailureIconPath.c_str(), sizeof(failureIconPathBuf) - 1);
                failureIconPathInitialized = true;
            }
            if (ImGuiMCP::InputText("Failure Icon Path", failureIconPathBuf, sizeof(failureIconPathBuf))) {
                settings->IconOverlay.FailureIconPath = failureIconPathBuf;
                changed = true;
            }
            ImGuiMCP::SetItemTooltip("Icon shown when PlayIdle fails (e.g., Data\\Interface\\ImGuiIcons\\Icons\\vampireFangs_fail.png)");
        }

        ImGuiMCP::Separator();
        ImGuiMCP::TextDisabled("Victim Health Bar");
        changed |= ImGuiMCP::Checkbox("Show Victim Health Bar", &settings->HealthBarOverlay.Enable);
        ImGuiMCP::SetItemTooltip("Draw the victim's health bar above their head for the duration of the composite feed");
        if (settings->HealthBarOverlay.Enable) {
            changed |= ImGuiMCP::SliderFloat("Bar Scale", &settings->HealthBarOverlay.Scale, 0.25f, 3.0f, "%.2fx");
            ImGuiMCP::SetItemTooltip("Size multiplier applied to bar width and height");
            changed |= ImGuiMCP::SliderFloat("Bar Width", &settings->HealthBarOverlay.Width, 40.0f, 400.0f, "%.0f px");
            changed |= ImGuiMCP::SliderFloat("Bar Height", &settings->HealthBarOverlay.Height, 4.0f, 40.0f, "%.0f px");
            changed |= ImGuiMCP::SliderFloat("Height Above Head", &settings->HealthBarOverlay.HeightOffset, -50.0f, 150.0f, "%.1f");
            ImGuiMCP::SetItemTooltip("Vertical offset above the victim's head (game units)");
            changed |= ImGuiMCP::SliderFloat("Screen Offset X", &settings->HealthBarOverlay.OffsetX, -300.0f, 300.0f, "%.0f px");
            ImGuiMCP::SetItemTooltip("Fine-tune horizontal screen position (+ = right)");
            changed |= ImGuiMCP::SliderFloat("Screen Offset Y", &settings->HealthBarOverlay.OffsetY, -300.0f, 300.0f, "%.0f px");
            ImGuiMCP::SetItemTooltip("Fine-tune vertical screen position (+ = down)");
            changed |= ImGuiMCP::Checkbox("Trailing Damage Bar", &settings->HealthBarOverlay.EnableTrailing);
            ImGuiMCP::SetItemTooltip("Show a lagging 'chip' layer behind the bar that catches up after each drain");
            if (settings->HealthBarOverlay.EnableTrailing) {
                changed |= ImGuiMCP::SliderFloat("Trailing Delay (s)", &settings->HealthBarOverlay.TrailingDelay, 0.0f, 2.0f, "%.2f");
                ImGuiMCP::SetItemTooltip("How long the trailing layer holds before sliding down");
                changed |= ImGuiMCP::SliderFloat("Trailing Speed", &settings->HealthBarOverlay.TrailingSpeed, 0.1f, 5.0f, "%.2f");
                ImGuiMCP::SetItemTooltip("Catch-up speed (bar fractions per second)");
            }
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

        ImGuiMCP::Separator();
        ImGuiMCP::TextDisabled("Composite Paired Animation");
        changed |= ImGuiMCP::Checkbox("Use Composite Paired Animation", &settings->NonCombat.UseCompositePairedAnimation);
        changed |= ImGuiMCP::Checkbox("Use Composite Furniture Animation", &settings->NonCombat.UseCompositeFurnitureAnimation);
        ImGuiMCP::SetItemTooltip("Player-only bed/bedroll feeds: the player plays a side-of-bed clip while the sleeping victim stays in place");
        if (settings->NonCombat.UseCompositePairedAnimation) {
            ImGuiMCP::TextDisabled("Standing clips are loaded from the *_DFO.json composite packs");

            ImGuiMCP::Separator();
            ImGuiMCP::TextDisabled("Stage Timing (Intro/Exit timer-driven; Drained ended early by VFD_DrainedEnd)");
            changed |= ImGuiMCP::SliderFloat("Intro Duration", &settings->NonCombat.CompositeIntroDuration, 0.0f, 10.0f, "%.2f s");
            ImGuiMCP::SetItemTooltip("Seconds the Intro (GoTo) clip plays before the feeding Loop");
            changed |= ImGuiMCP::SliderFloat("Exit Duration", &settings->NonCombat.CompositeExitDuration, 0.0f, 10.0f, "%.2f s");
            ImGuiMCP::SetItemTooltip("Seconds the Exit (GoBack) clip plays before the player is freed and Drained begins");

            ImGuiMCP::TextDisabled("Drained Aftermath (timer cap; VFD_DrainedEnd ends it earlier)");
            changed |= ImGuiMCP::SliderFloat("Drained Min", &settings->NonCombat.CompositeDrainedDurationMin, 0.0f, 15.0f, "%.2f s");
            ImGuiMCP::SetItemTooltip("Min of the random fallback length, used only if the clip has no VFD_DrainedEnd event (0 = can skip)");
            changed |= ImGuiMCP::SliderFloat("Drained Max", &settings->NonCombat.CompositeDrainedDurationMax, 0.0f, 15.0f, "%.2f s");
            ImGuiMCP::SetItemTooltip("Max fallback length / safety cap. Set high to let VFD_DrainedEnd always end the stage on its own");
            // Keep the range valid: clamp Min <= Max whichever the user just moved.
            if (settings->NonCombat.CompositeDrainedDurationMin > settings->NonCombat.CompositeDrainedDurationMax) {
                settings->NonCombat.CompositeDrainedDurationMax = settings->NonCombat.CompositeDrainedDurationMin;
            }
        }
        changed |= ImGuiMCP::SliderFloat("Target Offset X", &settings->NonCombat.TargetOffsetX, -100.0f, 100.0f, "%.1f");
        changed |= ImGuiMCP::SliderFloat("Target Offset Y", &settings->NonCombat.TargetOffsetY, -100.0f, 100.0f, "%.1f");
        ImGuiMCP::SetItemTooltip("Distance from player along their heading (+ in front, - behind). ~20-30 for embrace/OStim-style anims.");
        changed |= ImGuiMCP::SliderFloat("Target Offset Z", &settings->NonCombat.TargetOffsetZ, -50.0f, 50.0f, "%.1f");
    }

    // Sound Settings
    if (ImGuiMCP::CollapsingHeader("Sound")) {
        // Default vampire feed sound (used across the mod: composite anim + integrations)
        static char feedSoundBuf[256] = "";
        static bool feedSoundInitialized = false;
        if (!feedSoundInitialized) {
            strncpy(feedSoundBuf, settings->Animation.FeedSoundForm.c_str(), sizeof(feedSoundBuf) - 1);
            feedSoundInitialized = true;
        }
        if (ImGuiMCP::InputText("Feed Sound Form", feedSoundBuf, sizeof(feedSoundBuf))) {
            settings->Animation.FeedSoundForm = feedSoundBuf;
            changed = true;
        }
        ImGuiMCP::SetItemTooltip("Default vampire feed sound played during feeds (composite animation + Sacrosanct/Sacrilege integrations). Format: PluginName|0xFormID. Empty = disabled. Default: Skyrim.esm|0x0FF984 (NPCHumanVampireFeed).");
        if (ImGuiMCP::Button("Test Feed Sound")) {
            SoundUtil::PlayFeedSoundTest();
        }
        ImGuiMCP::SetItemTooltip("Play the currently configured feed sound once at the player (preview).");

        ImGuiMCP::Separator();
        static char failureSoundBuf[256] = "";
        static bool failureSoundInitialized = false;
        if (!failureSoundInitialized) {
            strncpy(failureSoundBuf, settings->Animation.FailureSoundForm.c_str(), sizeof(failureSoundBuf) - 1);
            failureSoundInitialized = true;
        }
        if (ImGuiMCP::InputText("Failure Sound Form", failureSoundBuf, sizeof(failureSoundBuf))) {
            settings->Animation.FailureSoundForm = failureSoundBuf;
            changed = true;
        }
        ImGuiMCP::SetItemTooltip("Sound played at player on PlayIdle failure. Format: PluginName|0xFormID. Empty = disabled. Default: Skyrim.esm|0x3C73C (WPNBlockBlade1HandVsOtherSD).");
        if (ImGuiMCP::Button("Test Failure Sound")) {
            AnimUtil::PlayFailureSoundTest();
        }
        ImGuiMCP::SetItemTooltip("Play the currently configured failure sound once (preview).");
    }

    // Health Drain Settings
    if (ImGuiMCP::CollapsingHeader("Health Drain")) {
        changed |= ImGuiMCP::Checkbox("Enable Health Drain", &settings->HealthDrain.Enable);
        ImGuiMCP::SetItemTooltip("Visual HP-bar drain on VFD_VampireFeedTrigger animation events");
        if (settings->HealthDrain.Enable) {
            changed |= ImGuiMCP::Checkbox("Floor Target At 1 HP", &settings->HealthDrain.FloorTargetAtOneHP);
            ImGuiMCP::SetItemTooltip("Target NPC only: floor drain at 1 HP so it never kills (paired animation delivers the kill). Disable to let drain take NPC to 0. Player drain always floors at 1.");
            changed |= ImGuiMCP::Checkbox("Drain On NPC", &settings->HealthDrain.DrainOnNPC);
            ImGuiMCP::SetItemTooltip("Apply the drain chunk to the target NPC each trigger");
            ImGuiMCP::Separator();
            ImGuiMCP::TextDisabled("Lethal feeds (variance + escalation)");
            changed |= ImGuiMCP::SliderFloat("Lethal Chunk Min %", &settings->HealthDrain.LethalChunkMinPercent, 1.0f, 100.0f, "%.1f%%");
            changed |= ImGuiMCP::SliderFloat("Lethal Chunk Max %", &settings->HealthDrain.LethalChunkMaxPercent, 1.0f, 100.0f, "%.1f%%");
            changed |= ImGuiMCP::SliderFloat("Escalation Per Trigger", &settings->HealthDrain.EscalationPerTrigger, 1.0f, 3.0f, "%.2fx");
            ImGuiMCP::SetItemTooltip("Multiplier applied to the roll for each successive trigger in same feed (1.0 = no escalation)");
            changed |= ImGuiMCP::SliderFloat("Max Chunk Cap %", &settings->HealthDrain.MaxChunkCapPercent, 50.0f, 100.0f, "%.1f%%");
            ImGuiMCP::SetItemTooltip("Safety cap so escalation can't one-shot to 1 HP");
            ImGuiMCP::Separator();
            ImGuiMCP::TextDisabled("Non-lethal feeds (fixed, no variance)");
            changed |= ImGuiMCP::SliderFloat("Non-Lethal Chunk %", &settings->HealthDrain.NonLethalChunkPercent, 1.0f, 100.0f, "%.1f%%");
        }

        ImGuiMCP::Separator();
        ImGuiMCP::TextDisabled("Composite feed (gulps)");
        ImGuiMCP::SetItemTooltip("The composite feeding loop drains HP in randomized chunks ('gulps') at randomized intervals");
        changed |= ImGuiMCP::SliderFloat("Gulp Interval Min (s)", &settings->HealthDrain.GulpIntervalMin, 0.1f, 5.0f, "%.2f");
        changed |= ImGuiMCP::SliderFloat("Gulp Interval Max (s)", &settings->HealthDrain.GulpIntervalMax, 0.1f, 5.0f, "%.2f");
        ImGuiMCP::SetItemTooltip("Seconds between gulps (randomized between min and max)");
        changed |= ImGuiMCP::SliderFloat("Gulp Drain Min %", &settings->HealthDrain.GulpPercentMin, 1.0f, 50.0f, "%.1f%%");
        changed |= ImGuiMCP::SliderFloat("Gulp Drain Max %", &settings->HealthDrain.GulpPercentMax, 1.0f, 50.0f, "%.1f%%");
        ImGuiMCP::SetItemTooltip("HP drained per gulp as a percent of the victim's max health");
        changed |= ImGuiMCP::SliderFloat("Gulp Lethal Threshold", &settings->HealthDrain.GulpLethalThreshold, 0.0f, 0.5f, "%.2f");
        ImGuiMCP::SetItemTooltip("Victim HP fraction at/below which the feeding loop drains dry and kills");
        changed |= ImGuiMCP::SliderFloat("Gulp Protected Floor", &settings->HealthDrain.GulpProtectedFloor, 0.0f, 0.5f, "%.2f");
        ImGuiMCP::SetItemTooltip("HP fraction floor for essential/protected victims (Exclude Essential From Lethal): drained to here but never killed");
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
