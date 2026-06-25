#include "PCH.h"
#include "Settings.h"
#include "utils/log.h"
#include <sstream>

// Helper to parse comma-separated string into vector
static std::vector<std::string> ParseKeywordList(const char* str) {
    std::vector<std::string> result;
    if (!str || strlen(str) == 0) return result;

    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            result.push_back(token.substr(start, end - start + 1));
        }
    }
    return result;
}

// Helper to join vector into comma-separated string
static std::string JoinKeywordList(const std::vector<std::string>& list) {
    std::string result;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) result += ", ";
        result += list[i];
    }
    return result;
}

// Helper to parse comma-separated int list
static std::vector<int> ParseIntList(const char* str) {
    std::vector<int> result;
    if (!str || strlen(str) == 0) return result;

    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (start != std::string::npos && end != std::string::npos) {
            try {
                result.push_back(std::stoi(token.substr(start, end - start + 1)));
            } catch (...) {
                // Skip invalid entries
            }
        }
    }
    return result;
}

// Helper to join int vector into comma-separated string
static std::string JoinIntList(const std::vector<int>& list) {
    std::string result;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) result += ", ";
        result += std::to_string(list[i]);
    }
    return result;
}

void Settings::LoadINI() {
    CSimpleIniA ini;
    ini.SetUnicode();

    SI_Error rc = ini.LoadFile(INI_PATH);
    if (rc < 0) {
        SKSE::log::info("No INI file found at {}, using defaults and creating file",
            std::filesystem::path(INI_PATH).string());
        SaveINI();
        return;
    }

    SKSE::log::info("Loading settings from INI...");

    // General
    General.EnableMod = ini.GetBoolValue("General", "EnableMod", General.EnableMod);
    General.LogLevel = ini.GetValue("General", "LogLevel", General.LogLevel.c_str());
    General.EnableWerewolf = ini.GetBoolValue("General", "EnableWerewolf", General.EnableWerewolf);
    General.EnableVampireLord = ini.GetBoolValue("General", "EnableVampireLord", General.EnableVampireLord);

    // Apply the configured log verbosity
    ApplyLogLevel(ParseLogLevel(General.LogLevel));

    General.ForceVampire = ini.GetBoolValue("General", "ForceVampire", General.ForceVampire);
    General.CheckHungerStage = ini.GetBoolValue("General", "CheckHungerStage", General.CheckHungerStage);
    General.MinHungerStage = static_cast<int>(ini.GetLongValue("General", "MinHungerStage", General.MinHungerStage));
    General.ForceFeedType = static_cast<int>(ini.GetLongValue("General", "ForceFeedType", General.ForceFeedType));
    General.DebugAnimationCycle = ini.GetBoolValue("General", "DebugAnimationCycle", General.DebugAnimationCycle);
    General.AnimationTimeout = static_cast<float>(ini.GetDoubleValue("General", "AnimationTimeout", General.AnimationTimeout));
    General.PeriodicCheckInterval = static_cast<float>(ini.GetDoubleValue("General", "PeriodicCheckInterval", General.PeriodicCheckInterval));
    General.PromptDelayIdleSeconds = static_cast<float>(ini.GetDoubleValue("General", "PromptDelayIdleSeconds", General.PromptDelayIdleSeconds));

    // Input
    Input.FeedKey = static_cast<int>(ini.GetLongValue("Input", "FeedKey", Input.FeedKey));
    Input.FeedGamepadKey = static_cast<int>(ini.GetLongValue("Input", "FeedGamepadKey", Input.FeedGamepadKey));
    Input.SecondaryKey = static_cast<int>(ini.GetLongValue("Input", "SecondaryKey", Input.SecondaryKey));
    Input.SecondaryGamepadKey = static_cast<int>(ini.GetLongValue("Input", "SecondaryGamepadKey", Input.SecondaryGamepadKey));

    // PromptDisplay
    PromptDisplay.RequireWeaponDrawn = ini.GetBoolValue("PromptDisplay", "RequireWeaponDrawn", PromptDisplay.RequireWeaponDrawn);
    PromptDisplay.ShowWhenSneaking = ini.GetBoolValue("PromptDisplay", "ShowWhenSneaking", PromptDisplay.ShowWhenSneaking);
    PromptDisplay.RequirePlayerFacing = ini.GetBoolValue("PromptDisplay", "RequirePlayerFacing", PromptDisplay.RequirePlayerFacing);
    PromptDisplay.FacingAngleThreshold = static_cast<float>(ini.GetDoubleValue("PromptDisplay", "FacingAngleThreshold", PromptDisplay.FacingAngleThreshold));
    PromptDisplay.RelaxedCombatTargeting = ini.GetBoolValue("PromptDisplay", "RelaxedCombatTargeting", PromptDisplay.RelaxedCombatTargeting);
    PromptDisplay.MaxTargetDistance = static_cast<float>(ini.GetDoubleValue("PromptDisplay", "MaxTargetDistance", PromptDisplay.MaxTargetDistance));

    // NonCombat
    NonCombat.AllowStanding = ini.GetBoolValue("NonCombat", "AllowStanding", NonCombat.AllowStanding);
    NonCombat.AllowSleeping = ini.GetBoolValue("NonCombat", "AllowSleeping", NonCombat.AllowSleeping);
    NonCombat.AllowSittingChair = ini.GetBoolValue("NonCombat", "AllowSittingChair", NonCombat.AllowSittingChair);
    NonCombat.EnableHeightAdjust = ini.GetBoolValue("NonCombat", "EnableHeightAdjust", NonCombat.EnableHeightAdjust);
    NonCombat.MinHeightDiff = static_cast<float>(ini.GetDoubleValue("NonCombat", "MinHeightDiff", NonCombat.MinHeightDiff));
    NonCombat.MaxHeightDiff = static_cast<float>(ini.GetDoubleValue("NonCombat", "MaxHeightDiff", NonCombat.MaxHeightDiff));
    NonCombat.UseCompositePairedAnimation = ini.GetBoolValue("NonCombat", "UseCompositePairedAnimation", NonCombat.UseCompositePairedAnimation);
    NonCombat.UseCompositeFurnitureAnimation = ini.GetBoolValue("NonCombat", "UseCompositeFurnitureAnimation", NonCombat.UseCompositeFurnitureAnimation);
    NonCombat.CompositeIntroDuration = static_cast<float>(ini.GetDoubleValue("NonCombat", "CompositeIntroDuration", NonCombat.CompositeIntroDuration));
    NonCombat.CompositeExitDuration = static_cast<float>(ini.GetDoubleValue("NonCombat", "CompositeExitDuration", NonCombat.CompositeExitDuration));
    NonCombat.CompositeDrainedDurationMin = static_cast<float>(ini.GetDoubleValue("NonCombat", "CompositeDrainedDurationMin", NonCombat.CompositeDrainedDurationMin));
    NonCombat.CompositeDrainedDurationMax = static_cast<float>(ini.GetDoubleValue("NonCombat", "CompositeDrainedDurationMax", NonCombat.CompositeDrainedDurationMax));
    NonCombat.TargetOffsetX = static_cast<float>(ini.GetDoubleValue("NonCombat", "TargetOffsetX", NonCombat.TargetOffsetX));
    NonCombat.TargetOffsetY = static_cast<float>(ini.GetDoubleValue("NonCombat", "TargetOffsetY", NonCombat.TargetOffsetY));
    NonCombat.TargetOffsetZ = static_cast<float>(ini.GetDoubleValue("NonCombat", "TargetOffsetZ", NonCombat.TargetOffsetZ));
    NonCombat.EnableLethalFeed = ini.GetBoolValue("NonCombat", "EnableLethalFeed", NonCombat.EnableLethalFeed);
    NonCombat.LethalHoldDuration = static_cast<float>(ini.GetDoubleValue("NonCombat", "LethalHoldDuration", NonCombat.LethalHoldDuration));
    NonCombat.ExcludeEssentialFromLethal = ini.GetBoolValue("NonCombat", "ExcludeEssentialFromLethal", NonCombat.ExcludeEssentialFromLethal);
    NonCombat.EnableRotation = ini.GetBoolValue("NonCombat", "EnableRotation", NonCombat.EnableRotation);
    NonCombat.EnableLevelCheck = ini.GetBoolValue("NonCombat", "EnableLevelCheck", NonCombat.EnableLevelCheck);
    NonCombat.MaxLevelDifference = static_cast<int>(ini.GetLongValue("NonCombat", "MaxLevelDifference", NonCombat.MaxLevelDifference));

    // Combat
    Combat.Enabled = ini.GetBoolValue("Combat", "Enabled", Combat.Enabled);
    Combat.IgnoreHungerCheck = ini.GetBoolValue("Combat", "IgnoreHungerCheck", Combat.IgnoreHungerCheck);
    Combat.RequireLowHealth = ini.GetBoolValue("Combat", "RequireLowHealth", Combat.RequireLowHealth);
    Combat.LowHealthThreshold = static_cast<float>(ini.GetDoubleValue("Combat", "LowHealthThreshold", Combat.LowHealthThreshold));
    Combat.AllowStaggered = ini.GetBoolValue("Combat", "AllowStaggered", Combat.AllowStaggered);
    Combat.StaggerRequireLowerLevel = ini.GetBoolValue("Combat", "StaggerRequireLowerLevel", Combat.StaggerRequireLowerLevel);
    Combat.StaggerMaxLevelDifference = static_cast<int>(ini.GetLongValue("Combat", "StaggerMaxLevelDifference", Combat.StaggerMaxLevelDifference));
    Combat.EnableWitnessDetection = ini.GetBoolValue("Combat", "EnableWitnessDetection", Combat.EnableWitnessDetection);
    Combat.WitnessDetectionRadius = static_cast<float>(ini.GetDoubleValue("Combat", "WitnessDetectionRadius", Combat.WitnessDetectionRadius));
    Combat.WitnessCheckInterval = static_cast<float>(ini.GetDoubleValue("Combat", "WitnessCheckInterval", Combat.WitnessCheckInterval));
    Combat.WitnessDebugLogging = ini.GetBoolValue("Combat", "WitnessDebugLogging", Combat.WitnessDebugLogging);
    Combat.PromptDelayCombatSeconds = static_cast<float>(ini.GetDoubleValue("Combat", "PromptDelayCombatSeconds", Combat.PromptDelayCombatSeconds));
    Combat.EnableWitnessCombatReaction = ini.GetBoolValue("Combat", "EnableWitnessCombatReaction", Combat.EnableWitnessCombatReaction);
    Combat.AssaultConfidenceThreshold = static_cast<int>(ini.GetLongValue("Combat", "AssaultConfidenceThreshold", Combat.AssaultConfidenceThreshold));
    Combat.WitnessRelationshipAware = ini.GetBoolValue("Combat", "WitnessRelationshipAware", Combat.WitnessRelationshipAware);

    // Filtering
    Filtering.ExcludeInScene = ini.GetBoolValue("Filtering", "ExcludeInScene", Filtering.ExcludeInScene);
    Filtering.ExcludeOStimScenes = ini.GetBoolValue("Filtering", "ExcludeOStimScenes", Filtering.ExcludeOStimScenes);
    Filtering.ExcludeDead = ini.GetBoolValue("Filtering", "ExcludeDead", Filtering.ExcludeDead);
    Filtering.AllowRecentlyDead = ini.GetBoolValue("Filtering", "AllowRecentlyDead", Filtering.AllowRecentlyDead);
    Filtering.MaxDeadHours = static_cast<float>(ini.GetDoubleValue("Filtering", "MaxDeadHours", Filtering.MaxDeadHours));
    Filtering.MaxDeadFeeds = static_cast<int>(ini.GetLongValue("Filtering", "MaxDeadFeeds", Filtering.MaxDeadFeeds));
    Filtering.IncludeKeywords = ParseKeywordList(ini.GetValue("Filtering", "IncludeKeywords", ""));
    Filtering.ExcludeKeywords = ParseKeywordList(ini.GetValue("Filtering", "ExcludeKeywords", ""));
    Filtering.ExcludeActorIDs = ParseKeywordList(ini.GetValue("Filtering", "ExcludeActorIDs", ""));

    // IconOverlay
    IconOverlay.EnableIconOverlay = ini.GetBoolValue("IconOverlay", "EnableIconOverlay", IconOverlay.EnableIconOverlay);
    IconOverlay.IconPosition = static_cast<int>(ini.GetLongValue("IconOverlay", "IconPosition", IconOverlay.IconPosition));
    IconOverlay.IconDuration = static_cast<float>(ini.GetDoubleValue("IconOverlay", "IconDuration", IconOverlay.IconDuration));
    IconOverlay.IconSize = static_cast<float>(ini.GetDoubleValue("IconOverlay", "IconSize", IconOverlay.IconSize));
    IconOverlay.IconHeightOffset = static_cast<float>(ini.GetDoubleValue("IconOverlay", "IconHeightOffset", IconOverlay.IconHeightOffset));
    IconOverlay.IconPath = ini.GetValue("IconOverlay", "IconPath", IconOverlay.IconPath.c_str());
    IconOverlay.FailureIconPath = ini.GetValue("IconOverlay", "FailureIconPath", IconOverlay.FailureIconPath.c_str());

    // HealthBarOverlay
    HealthBarOverlay.Enable = ini.GetBoolValue("HealthBarOverlay", "Enable", HealthBarOverlay.Enable);
    HealthBarOverlay.Width = static_cast<float>(ini.GetDoubleValue("HealthBarOverlay", "Width", HealthBarOverlay.Width));
    HealthBarOverlay.Height = static_cast<float>(ini.GetDoubleValue("HealthBarOverlay", "Height", HealthBarOverlay.Height));
    HealthBarOverlay.Scale = static_cast<float>(ini.GetDoubleValue("HealthBarOverlay", "Scale", HealthBarOverlay.Scale));
    HealthBarOverlay.HeightOffset = static_cast<float>(ini.GetDoubleValue("HealthBarOverlay", "HeightOffset", HealthBarOverlay.HeightOffset));
    HealthBarOverlay.OffsetX = static_cast<float>(ini.GetDoubleValue("HealthBarOverlay", "OffsetX", HealthBarOverlay.OffsetX));
    HealthBarOverlay.OffsetY = static_cast<float>(ini.GetDoubleValue("HealthBarOverlay", "OffsetY", HealthBarOverlay.OffsetY));
    HealthBarOverlay.EnableTrailing = ini.GetBoolValue("HealthBarOverlay", "EnableTrailing", HealthBarOverlay.EnableTrailing);
    HealthBarOverlay.TrailingDelay = static_cast<float>(ini.GetDoubleValue("HealthBarOverlay", "TrailingDelay", HealthBarOverlay.TrailingDelay));
    HealthBarOverlay.TrailingSpeed = static_cast<float>(ini.GetDoubleValue("HealthBarOverlay", "TrailingSpeed", HealthBarOverlay.TrailingSpeed));

    // Animation
    Animation.EnableRandomSelection = ini.GetBoolValue("Animation", "EnableRandomSelection", Animation.EnableRandomSelection);
    Animation.HungryThreshold = static_cast<int>(ini.GetLongValue("Animation", "HungryThreshold", Animation.HungryThreshold));
    Animation.EnableTimeSlowdown = ini.GetBoolValue("Animation", "EnableTimeSlowdown", Animation.EnableTimeSlowdown);
    Animation.TimeSlowdownMultiplier = static_cast<float>(ini.GetDoubleValue("Animation", "TimeSlowdownMultiplier", Animation.TimeSlowdownMultiplier));
    Animation.FeedSoundForm = ini.GetValue("Animation", "FeedSoundForm", Animation.FeedSoundForm.c_str());
    Animation.FailureSoundForm = ini.GetValue("Animation", "FailureSoundForm", Animation.FailureSoundForm.c_str());

    // HealthDrain
    HealthDrain.Enable = ini.GetBoolValue("HealthDrain", "Enable", HealthDrain.Enable);
    HealthDrain.FloorTargetAtOneHP = ini.GetBoolValue("HealthDrain", "FloorTargetAtOneHP", HealthDrain.FloorTargetAtOneHP);
    HealthDrain.DrainOnNPC = ini.GetBoolValue("HealthDrain", "DrainOnNPC", HealthDrain.DrainOnNPC);
    HealthDrain.LethalChunkMinPercent = static_cast<float>(ini.GetDoubleValue("HealthDrain", "LethalChunkMinPercent", HealthDrain.LethalChunkMinPercent));
    HealthDrain.LethalChunkMaxPercent = static_cast<float>(ini.GetDoubleValue("HealthDrain", "LethalChunkMaxPercent", HealthDrain.LethalChunkMaxPercent));
    HealthDrain.EscalationPerTrigger = static_cast<float>(ini.GetDoubleValue("HealthDrain", "EscalationPerTrigger", HealthDrain.EscalationPerTrigger));
    HealthDrain.NonLethalChunkPercent = static_cast<float>(ini.GetDoubleValue("HealthDrain", "NonLethalChunkPercent", HealthDrain.NonLethalChunkPercent));
    HealthDrain.MaxChunkCapPercent = static_cast<float>(ini.GetDoubleValue("HealthDrain", "MaxChunkCapPercent", HealthDrain.MaxChunkCapPercent));
    HealthDrain.GulpIntervalMin = static_cast<float>(ini.GetDoubleValue("HealthDrain", "GulpIntervalMin", HealthDrain.GulpIntervalMin));
    HealthDrain.GulpIntervalMax = static_cast<float>(ini.GetDoubleValue("HealthDrain", "GulpIntervalMax", HealthDrain.GulpIntervalMax));
    HealthDrain.GulpPercentMin = static_cast<float>(ini.GetDoubleValue("HealthDrain", "GulpPercentMin", HealthDrain.GulpPercentMin));
    HealthDrain.GulpPercentMax = static_cast<float>(ini.GetDoubleValue("HealthDrain", "GulpPercentMax", HealthDrain.GulpPercentMax));
    HealthDrain.GulpLethalThreshold = static_cast<float>(ini.GetDoubleValue("HealthDrain", "GulpLethalThreshold", HealthDrain.GulpLethalThreshold));
    HealthDrain.GulpProtectedFloor = static_cast<float>(ini.GetDoubleValue("HealthDrain", "GulpProtectedFloor", HealthDrain.GulpProtectedFloor));

    // Integration
    Integration.EnableSacrosanct = ini.GetBoolValue("Integration", "EnableSacrosanct", Integration.EnableSacrosanct);
    Integration.EnableSacrilege = ini.GetBoolValue("Integration", "EnableSacrilege", Integration.EnableSacrilege);
    Integration.EnableBetterVampires = ini.GetBoolValue("Integration", "EnableBetterVampires", Integration.EnableBetterVampires);
    Integration.PoiseIgnoresLevelCheck = ini.GetBoolValue("Integration", "PoiseIgnoresLevelCheck", Integration.PoiseIgnoresLevelCheck);
    Integration.DeepSacrosanctIntegration = ini.GetBoolValue("Integration", "DeepSacrosanctIntegration", Integration.DeepSacrosanctIntegration);
    Integration.DeepSacrilegeIntegration = ini.GetBoolValue("Integration", "DeepSacrilegeIntegration", Integration.DeepSacrilegeIntegration);
    Integration.EnableSacrosanctInCombat = ini.GetBoolValue("Integration", "EnableSacrosanctInCombat", Integration.EnableSacrosanctInCombat);
    Integration.EnableSacrilegeInCombat = ini.GetBoolValue("Integration", "EnableSacrilegeInCombat", Integration.EnableSacrilegeInCombat);
    Integration.EnableVampireFeedProxy = ini.GetBoolValue("Integration", "EnableVampireFeedProxy", Integration.EnableVampireFeedProxy);

    SKSE::log::info("Settings loaded:");
    SKSE::log::info("  [General] EnableMod={}, LogLevel={}, Werewolf={}, VL={}, ForceVampire={}, CheckHunger={} (min={}), ForceFeedType={}, DebugAnimationCycle={}, AnimationTimeout={}, PeriodicCheckInterval={}, PromptDelaySeconds={}",
        General.EnableMod, General.LogLevel, General.EnableWerewolf, General.EnableVampireLord, General.ForceVampire,
        General.CheckHungerStage, General.MinHungerStage, General.ForceFeedType, General.DebugAnimationCycle, General.AnimationTimeout, General.PeriodicCheckInterval, General.PromptDelayIdleSeconds);
    SKSE::log::info("  [Input] FeedKey=0x{:X}, FeedGamepadKey=0x{:X}, SecondaryKey=0x{:X}, SecondaryGamepadKey=0x{:X}",
        Input.FeedKey, Input.FeedGamepadKey, Input.SecondaryKey, Input.SecondaryGamepadKey);
    SKSE::log::info("  [PromptDisplay] RequireWeaponDrawn={}, ShowWhenSneaking={}, RequirePlayerFacing={}, FacingAngleThreshold={}",
        PromptDisplay.RequireWeaponDrawn, PromptDisplay.ShowWhenSneaking, PromptDisplay.RequirePlayerFacing, PromptDisplay.FacingAngleThreshold);
    SKSE::log::info("  [NonCombat] Standing={}, Sleeping={}, SittingChair={}, HeightAdjust={} (min={}, max={}), CompositePaired={}, CompositeFurniture={}, EnableLethalFeed={}, LethalHoldDuration={}, ExcludeEssentialFromLethal={}, EnableLevelCheck={}, MaxLevelDiff={}",
        NonCombat.AllowStanding, NonCombat.AllowSleeping, NonCombat.AllowSittingChair,
        NonCombat.EnableHeightAdjust, NonCombat.MinHeightDiff, NonCombat.MaxHeightDiff,
        NonCombat.UseCompositePairedAnimation, NonCombat.UseCompositeFurnitureAnimation, NonCombat.EnableLethalFeed, NonCombat.LethalHoldDuration, NonCombat.ExcludeEssentialFromLethal,
        NonCombat.EnableLevelCheck, NonCombat.MaxLevelDifference);
    SKSE::log::info("  [Combat] Enabled={}, IgnoreHungerCheck={}, RequireLowHealth={}, LowHealthThreshold={}, AllowStaggered={}, StaggerRequireLowerLevel={}, StaggerMaxLevelDiff={}, WitnessDetection={}, WitnessRadius={}, WitnessInterval={}, WitnessDebugLog={}, PromptDelay={}, WitnessCombatReaction={}, AssaultConfThreshold={}, RelationshipAware={}",
        Combat.Enabled, Combat.IgnoreHungerCheck, Combat.RequireLowHealth, Combat.LowHealthThreshold, Combat.AllowStaggered,
        Combat.StaggerRequireLowerLevel, Combat.StaggerMaxLevelDifference,
        Combat.EnableWitnessDetection, Combat.WitnessDetectionRadius, Combat.WitnessCheckInterval, Combat.WitnessDebugLogging, Combat.PromptDelayCombatSeconds,
        Combat.EnableWitnessCombatReaction, Combat.AssaultConfidenceThreshold, Combat.WitnessRelationshipAware);
    SKSE::log::info("  [Filtering] ExcludeInScene={}, ExcludeOStim={}, ExcludeDead={}, AllowRecentlyDead={}, MaxDeadHours={}, MaxDeadFeeds={}, IncludeKW=[{}], ExcludeKW=[{}], ExcludeActorIDs=[{}]",
        Filtering.ExcludeInScene, Filtering.ExcludeOStimScenes, Filtering.ExcludeDead,
        Filtering.AllowRecentlyDead, Filtering.MaxDeadHours, Filtering.MaxDeadFeeds,
        JoinKeywordList(Filtering.IncludeKeywords), JoinKeywordList(Filtering.ExcludeKeywords), JoinKeywordList(Filtering.ExcludeActorIDs));
    SKSE::log::info("  [Animation] EnableRandom={}, HungryThreshold={}, EnableTimeSlowdown={}, TimeSlowdownMultiplier={}, FeedSoundForm='{}', FailureSoundForm='{}'",
        Animation.EnableRandomSelection, Animation.HungryThreshold, Animation.EnableTimeSlowdown, Animation.TimeSlowdownMultiplier,
        Animation.FeedSoundForm, Animation.FailureSoundForm);
    SKSE::log::info("  [HealthDrain] Enable={}, FloorTargetAtOneHP={}, OnNPC={}, LethalMin={}, LethalMax={}, Escalation={}, NonLethal={}, Cap={}",
        HealthDrain.Enable, HealthDrain.FloorTargetAtOneHP, HealthDrain.DrainOnNPC,
        HealthDrain.LethalChunkMinPercent, HealthDrain.LethalChunkMaxPercent,
        HealthDrain.EscalationPerTrigger, HealthDrain.NonLethalChunkPercent, HealthDrain.MaxChunkCapPercent);
    SKSE::log::info("  [Integration] EnableSacrosanct={}, EnableSacrilege={}, EnableBetterVampires={}, PoiseIgnoresLevelCheck={}, DeepSacrosanct={}, DeepSacrilege={}, SacrosanctInCombat={}, SacrilegeInCombat={}",
        Integration.EnableSacrosanct, Integration.EnableSacrilege, Integration.EnableBetterVampires, Integration.PoiseIgnoresLevelCheck,
        Integration.DeepSacrosanctIntegration, Integration.DeepSacrilegeIntegration, Integration.EnableSacrosanctInCombat, Integration.EnableSacrilegeInCombat);
}

void Settings::SaveINI() {
    CSimpleIniA ini;
    ini.SetUnicode();

    // General
    ini.SetBoolValue("General", "EnableMod", General.EnableMod,
        "; Enable or disable the entire mod");
    ini.SetValue("General", "LogLevel", General.LogLevel.c_str(),
        "; Log verbosity: trace, debug, info, warn, error");
    ini.SetBoolValue("General", "EnableWerewolf", General.EnableWerewolf,
        "; Enable for Werewolf form (EXPERIMENTAL: May be buggy and needs more work)");
    ini.SetBoolValue("General", "EnableVampireLord", General.EnableVampireLord,
        "; Enable for Vampire Lord form");
    ini.SetBoolValue("General", "ForceVampire", General.ForceVampire,
        "; Debug: skip vampire check, always allow feeding");
    ini.SetBoolValue("General", "CheckHungerStage", General.CheckHungerStage,
        "; Only allow feeding if vampire hunger stage >= MinHungerStage");
    ini.SetLongValue("General", "MinHungerStage", General.MinHungerStage,
        "; Minimum hunger stage required to feed (1-4, where 4 is most hungry)");
    ini.SetLongValue("General", "ForceFeedType", General.ForceFeedType,
        "; Debug: force specific FeedType (0=auto, 11-14=standing, 21-24=sleeping, 31-34=sitting, 41-44=combat)");
    ini.SetBoolValue("General", "DebugAnimationCycle", General.DebugAnimationCycle,
        "; Debug: cycle through all animations sequentially");
    ini.SetDoubleValue("General", "AnimationTimeout", General.AnimationTimeout,
        "; Timeout for animation events in seconds (default 15.0)");
    ini.SetDoubleValue("General", "PeriodicCheckInterval", General.PeriodicCheckInterval,
        "; Interval in seconds for periodic validity checks (default 1.0)");
    ini.SetDoubleValue("General", "PromptDelayIdleSeconds", General.PromptDelayIdleSeconds,
        "; Delay in seconds before showing prompt when targeting a new NPC outside combat (default 0.2)");

    // Input
    ini.SetLongValue("Input", "FeedKey", Input.FeedKey,
        "; Keyboard key code for primary feed prompt (default 0x22 = G)");
    ini.SetLongValue("Input", "FeedGamepadKey", Input.FeedGamepadKey,
        "; Gamepad key code for primary feed prompt (default 0x1000 = A)");
    ini.SetLongValue("Input", "SecondaryKey", Input.SecondaryKey,
        "; Keyboard key code for secondary prompt - Embrace (Sacrosanct) (default 0x23 = H)");
    ini.SetLongValue("Input", "SecondaryGamepadKey", Input.SecondaryGamepadKey,
        "; Gamepad key code for secondary prompt - Embrace (Sacrosanct) (default 0x2000 = B)");

    // PromptDisplay
    ini.SetBoolValue("PromptDisplay", "RequireWeaponDrawn", PromptDisplay.RequireWeaponDrawn,
        "; Only show feed prompt when weapon/magic is drawn or player is in combat");
    ini.SetBoolValue("PromptDisplay", "ShowWhenSneaking", PromptDisplay.ShowWhenSneaking,
        "; Show feed prompt when player is sneaking (enables stealth takedowns via vampire feed)");
    ini.SetBoolValue("PromptDisplay", "RequirePlayerFacing", PromptDisplay.RequirePlayerFacing,
        "; Only show feed prompt when player is facing the target");
    ini.SetDoubleValue("PromptDisplay", "FacingAngleThreshold", PromptDisplay.FacingAngleThreshold,
        "; Maximum angle (degrees) from player heading to target (90 = 180 degree cone in front)");
    ini.SetBoolValue("PromptDisplay", "RelaxedCombatTargeting", PromptDisplay.RelaxedCombatTargeting,
        "; Disable player facing requirement during combat (easier target selection in combat)");
    ini.SetDoubleValue("PromptDisplay", "MaxTargetDistance", PromptDisplay.MaxTargetDistance,
        "; Maximum distance to target for feed prompt to show (units, 250 = ~5 meters)");

    // NonCombat
    ini.SetBoolValue("NonCombat", "AllowStanding", NonCombat.AllowStanding,
        "; Allow feeding on standing NPCs");
    ini.SetBoolValue("NonCombat", "AllowSleeping", NonCombat.AllowSleeping,
        "; Allow feeding on sleeping NPCs");
    ini.SetBoolValue("NonCombat", "AllowSittingChair", NonCombat.AllowSittingChair,
        "; Allow feeding on NPCs sitting in chairs (no vanilla animation)");
    ini.SetBoolValue("NonCombat", "EnableHeightAdjust", NonCombat.EnableHeightAdjust,
        "; Adjust actor positions when on stairs to fix animation issues");
    ini.SetDoubleValue("NonCombat", "MinHeightDiff", NonCombat.MinHeightDiff,
        "; Minimum height difference to trigger adjustment (units)");
    ini.SetDoubleValue("NonCombat", "MaxHeightDiff", NonCombat.MaxHeightDiff,
        "; Maximum height difference for adjustment (~3-4 stair steps)");
    ini.SetBoolValue("NonCombat", "UseCompositePairedAnimation", NonCombat.UseCompositePairedAnimation,
        "; Composite two single-actor animations to simulate a paired animation (requires custom animations)");
    ini.SetBoolValue("NonCombat", "UseCompositeFurnitureAnimation", NonCombat.UseCompositeFurnitureAnimation,
        "; Use player-only composite packs for bed/bedroll feeds: the player plays a side-of-bed clip while the sleeping victim stays in place (requires custom furniture animations)");
    ini.SetDoubleValue("NonCombat", "CompositeIntroDuration", NonCombat.CompositeIntroDuration,
        "; Staged composite: seconds Intro plays before the feeding Loop (timer-driven)");
    ini.SetDoubleValue("NonCombat", "CompositeExitDuration", NonCombat.CompositeExitDuration,
        "; Staged composite: seconds Exit (GoBack) plays before the player is freed and Drained begins (timer-driven)");
    ini.SetDoubleValue("NonCombat", "CompositeDrainedDurationMin", NonCombat.CompositeDrainedDurationMin,
        "; Staged composite: Drained aftermath MIN seconds (a random length in [Min,Max] is rolled each feed; 0 = can skip)");
    ini.SetDoubleValue("NonCombat", "CompositeDrainedDurationMax", NonCombat.CompositeDrainedDurationMax,
        "; Staged composite: Drained aftermath MAX seconds (set up to the full Drained clip length, ~9.3s, to play it fully)");
    ini.SetDoubleValue("NonCombat", "TargetOffsetX", NonCombat.TargetOffsetX,
        "; Target X offset from player in local coordinates (0=centered)");
    ini.SetDoubleValue("NonCombat", "TargetOffsetY", NonCombat.TargetOffsetY,
        "; Target Y offset (positive=in front of player, 100=~1 meter)");
    ini.SetDoubleValue("NonCombat", "TargetOffsetZ", NonCombat.TargetOffsetZ,
        "; Target Z offset (height adjustment)");
    ini.SetBoolValue("NonCombat", "EnableLethalFeed", NonCombat.EnableLethalFeed,
        "; Enable hold-to-kill feature: Hold button for LethalHoldDuration to kill non-combat targets");
    ini.SetDoubleValue("NonCombat", "LethalHoldDuration", NonCombat.LethalHoldDuration,
        "; Seconds to hold button for lethal feed (default 5.0)");
    ini.SetBoolValue("NonCombat", "EnableRotation", NonCombat.EnableRotation,
        "; Rotate player and target to face each other before feed animation");
    ini.SetBoolValue("NonCombat", "EnableLevelCheck", NonCombat.EnableLevelCheck,
        "; Exclude targets above player level + MaxLevelDifference (non-combat only)");
    ini.SetLongValue("NonCombat", "MaxLevelDifference", NonCombat.MaxLevelDifference,
        "; Max levels target can be above player for non-combat feeding");

    // Combat
    ini.SetBoolValue("Combat", "Enabled", Combat.Enabled,
        "; Enable combat feeding");
    ini.SetBoolValue("Combat", "IgnoreHungerCheck", Combat.IgnoreHungerCheck,
        "; Allow combat feeding even when CheckHungerStage would block non-combat feeding");
    ini.SetBoolValue("Combat", "RequireLowHealth", Combat.RequireLowHealth,
        "; Require target to be below health threshold for combat feeding");
    ini.SetDoubleValue("Combat", "LowHealthThreshold", Combat.LowHealthThreshold,
        "; Health percentage threshold (0.0-1.0) for combat feeding");
    ini.SetBoolValue("Combat", "AllowStaggered", Combat.AllowStaggered,
        "; Allow feeding on staggered targets (bypasses health check)");
    ini.SetBoolValue("Combat", "StaggerRequireLowerLevel", Combat.StaggerRequireLowerLevel,
        "; Stagger feeding requires target to be lower level than player (ignored if poise mod detected)");
    ini.SetLongValue("Combat", "StaggerMaxLevelDifference", Combat.StaggerMaxLevelDifference,
        "; Target must be (playerLevel - this) or lower. E.g. player 20, diff 10 = target max level 10");
    ini.SetBoolValue("Combat", "EnableWitnessDetection", Combat.EnableWitnessDetection,
        "; Stop feed if witnessed by nearby NPCs who detect the player");
    ini.SetDoubleValue("Combat", "WitnessDetectionRadius", Combat.WitnessDetectionRadius,
        "; Maximum distance (units) to check for witnesses during feed (default 1500 = ~30 meters)");
    ini.SetDoubleValue("Combat", "WitnessCheckInterval", Combat.WitnessCheckInterval,
        "; How often to check for witnesses during active feed in seconds (default 0.5)");
    ini.SetBoolValue("Combat", "WitnessDebugLogging", Combat.WitnessDebugLogging,
        "; Enable verbose witness detection debug logging (can be very spammy)");
    ini.SetDoubleValue("Combat", "PromptDelayCombatSeconds", Combat.PromptDelayCombatSeconds,
        "; Delay in seconds before showing prompt in combat (default 0 for immediate response)");
    ini.SetBoolValue("Combat", "EnableWitnessCombatReaction", Combat.EnableWitnessCombatReaction,
        "; An awake victim who survives a feed fights back if brave enough; otherwise only the bounty applies");
    ini.SetLongValue("Combat", "AssaultConfidenceThreshold", Combat.AssaultConfidenceThreshold,
        "; Minimum victim Confidence to fight back when awake (0=Cowardly, 1=Cautious, 2=Average, 3=Brave, 4=Foolhardy)");
    ini.SetBoolValue("Combat", "WitnessRelationshipAware", Combat.WitnessRelationshipAware,
        "; Factor in relationship/faction: friends never attack or report, foes/enemies always attack, others decide by Confidence (off = Confidence only)");

    // Filtering
    ini.SetBoolValue("Filtering", "ExcludeInScene", Filtering.ExcludeInScene,
        "; Exclude targets currently in a scene (dialogues, scripted events)");
    ini.SetBoolValue("Filtering", "ExcludeOStimScenes", Filtering.ExcludeOStimScenes,
        "; Exclude targets in OStim NG scenes (auto-detects OStim, gracefully disabled if not installed)");
    ini.SetBoolValue("Filtering", "ExcludeDead", Filtering.ExcludeDead,
        "; Exclude dead actors from feeding");
    ini.SetBoolValue("Filtering", "AllowRecentlyDead", Filtering.AllowRecentlyDead,
        "; Allow feeding on recently dead actors (overrides ExcludeDead for fresh corpses)");
    ini.SetDoubleValue("Filtering", "MaxDeadHours", Filtering.MaxDeadHours,
        "; Maximum in-game hours since death to allow feeding (default 1.0 = 1 hour)");
    ini.SetLongValue("Filtering", "MaxDeadFeeds", Filtering.MaxDeadFeeds,
        "; Maximum times to feed on a single corpse (0=unlimited, resets on game reload)");
    ini.SetValue("Filtering", "IncludeKeywords", JoinKeywordList(Filtering.IncludeKeywords).c_str(),
        "; Only allow feeding if target has ANY of these keywords (comma-separated, empty=allow all)");
    ini.SetValue("Filtering", "ExcludeKeywords", JoinKeywordList(Filtering.ExcludeKeywords).c_str(),
        "; Never allow feeding if target has ANY of these keywords (comma-separated)");
    ini.SetValue("Filtering", "ExcludeActorIDs", JoinKeywordList(Filtering.ExcludeActorIDs).c_str(),
        "; Never allow feeding on specific NPCs by base form ID (format: PluginName|0xFormID, e.g., Dawnguard.esm|0x002B6C for Serana)");

    // IconOverlay
    ini.SetBoolValue("IconOverlay", "EnableIconOverlay", IconOverlay.EnableIconOverlay,
        "; Show vampire fang icon above target's head during feeding");
    ini.SetLongValue("IconOverlay", "IconPosition", IconOverlay.IconPosition,
        "; Icon position: 0=AboveHead, 1=RightOfHead");
    ini.SetDoubleValue("IconOverlay", "IconDuration", IconOverlay.IconDuration,
        "; How long to display icon in seconds");
    ini.SetDoubleValue("IconOverlay", "IconSize", IconOverlay.IconSize,
        "; Size of the icon in pixels");
    ini.SetDoubleValue("IconOverlay", "IconHeightOffset", IconOverlay.IconHeightOffset,
        "; Height offset above the target's head in game units (default 15.0)");
    ini.SetValue("IconOverlay", "IconPath", IconOverlay.IconPath.c_str(),
        "; Path to the icon image file (PNG, JPG, etc.)");
    ini.SetValue("IconOverlay", "FailureIconPath", IconOverlay.FailureIconPath.c_str(),
        "; Path to the icon shown when PlayIdle fails (PNG, JPG, etc.)");

    // HealthBarOverlay
    ini.SetBoolValue("HealthBarOverlay", "Enable", HealthBarOverlay.Enable,
        "; Show the victim's health bar above their head during a feed (custom overlay, always reliable)");
    ini.SetDoubleValue("HealthBarOverlay", "Width", HealthBarOverlay.Width,
        "; Health bar width in pixels");
    ini.SetDoubleValue("HealthBarOverlay", "Height", HealthBarOverlay.Height,
        "; Health bar height in pixels");
    ini.SetDoubleValue("HealthBarOverlay", "Scale", HealthBarOverlay.Scale,
        "; Size multiplier applied to the bar width and height (1.0 = default)");
    ini.SetDoubleValue("HealthBarOverlay", "HeightOffset", HealthBarOverlay.HeightOffset,
        "; Height of the bar above the victim's head (game units)");
    ini.SetDoubleValue("HealthBarOverlay", "OffsetX", HealthBarOverlay.OffsetX,
        "; Screen-space horizontal offset in pixels (+ = right)");
    ini.SetDoubleValue("HealthBarOverlay", "OffsetY", HealthBarOverlay.OffsetY,
        "; Screen-space vertical offset in pixels (+ = down)");
    ini.SetBoolValue("HealthBarOverlay", "EnableTrailing", HealthBarOverlay.EnableTrailing,
        "; Show a trailing 'damage chip' layer that lags behind the front bar to show recent drain");
    ini.SetDoubleValue("HealthBarOverlay", "TrailingDelay", HealthBarOverlay.TrailingDelay,
        "; Seconds the trailing layer holds before sliding down to catch up");
    ini.SetDoubleValue("HealthBarOverlay", "TrailingSpeed", HealthBarOverlay.TrailingSpeed,
        "; Catch-up speed of the trailing layer (bar fractions per second)");

    // Animation
    ini.SetBoolValue("Animation", "EnableRandomSelection", Animation.EnableRandomSelection,
        "; Enable random animation selection from available FeedType lists");
    ini.SetLongValue("Animation", "HungryThreshold", Animation.HungryThreshold,
        "; Hunger stage >= this uses hungry animations (1=sated, 2=peckish, 3=hungry, 4=starving)");
    ini.SetBoolValue("Animation", "EnableTimeSlowdown", Animation.EnableTimeSlowdown,
        "; Enable time slowdown effect when paired feed animation starts");
    ini.SetDoubleValue("Animation", "TimeSlowdownMultiplier", Animation.TimeSlowdownMultiplier,
        "; Time multiplier during feed (0.4 = 40% speed, 1.0 = normal speed)");
    ini.SetValue("Animation", "FeedSoundForm", Animation.FeedSoundForm.c_str(),
        "; Default vampire feed sound played during feeds (composite animation + Sacrosanct/Sacrilege integrations). Format: PluginName|0xFormID. Empty = disabled. Default: NPCHumanVampireFeed (Skyrim.esm|0x0FF984).");
    ini.SetValue("Animation", "FailureSoundForm", Animation.FailureSoundForm.c_str(),
        "; Sound played at player when feed animation fails to start (after all retries). Format: PluginName|0xFormID. Empty = disabled. Default: WPNBlockBlade1HandVsOtherSD (Skyrim.esm|0x3C73C).");

    // HealthDrain
    ini.SetBoolValue("HealthDrain", "Enable", HealthDrain.Enable,
        "; Enable visual HP drain on VFD_VampireFeedTrigger animation events");
    ini.SetBoolValue("HealthDrain", "FloorTargetAtOneHP", HealthDrain.FloorTargetAtOneHP,
        "; Target NPC only: floor drained HP at 1 so the drain itself can never kill (paired animation delivers the kill). Set false to let drain take the NPC to 0. Player drain ALWAYS floors at 1 regardless of this flag.");
    ini.SetBoolValue("HealthDrain", "DrainOnNPC", HealthDrain.DrainOnNPC,
        "; Apply the drain chunk to the target NPC each VFD_VampireFeedTrigger");
    ini.SetDoubleValue("HealthDrain", "LethalChunkMinPercent", HealthDrain.LethalChunkMinPercent,
        "; Lower bound (% of current HP) drained per trigger on lethal feeds");
    ini.SetDoubleValue("HealthDrain", "LethalChunkMaxPercent", HealthDrain.LethalChunkMaxPercent,
        "; Upper bound (% of current HP) drained per trigger on lethal feeds");
    ini.SetDoubleValue("HealthDrain", "EscalationPerTrigger", HealthDrain.EscalationPerTrigger,
        "; Multiplier applied to the roll for each successive trigger in same feed (1.0 = no escalation, 1.2 = +20%/trigger)");
    ini.SetDoubleValue("HealthDrain", "NonLethalChunkPercent", HealthDrain.NonLethalChunkPercent,
        "; Fixed % of current HP drained per trigger on non-lethal feeds (no variance, no escalation)");
    ini.SetDoubleValue("HealthDrain", "MaxChunkCapPercent", HealthDrain.MaxChunkCapPercent,
        "; Safety cap so escalation can't one-shot to 1 HP (default 95.0)");
    ini.SetDoubleValue("HealthDrain", "GulpIntervalMin", HealthDrain.GulpIntervalMin,
        "; Composite feed: minimum seconds between drain 'gulps'");
    ini.SetDoubleValue("HealthDrain", "GulpIntervalMax", HealthDrain.GulpIntervalMax,
        "; Composite feed: maximum seconds between drain 'gulps'");
    ini.SetDoubleValue("HealthDrain", "GulpPercentMin", HealthDrain.GulpPercentMin,
        "; Composite feed: minimum HP drained per gulp (% of victim max health)");
    ini.SetDoubleValue("HealthDrain", "GulpPercentMax", HealthDrain.GulpPercentMax,
        "; Composite feed: maximum HP drained per gulp (% of victim max health)");
    ini.SetDoubleValue("HealthDrain", "GulpLethalThreshold", HealthDrain.GulpLethalThreshold,
        "; Composite feed: victim HP fraction (0-1) at/below which the feeding loop drains dry and kills");
    ini.SetDoubleValue("HealthDrain", "GulpProtectedFloor", HealthDrain.GulpProtectedFloor,
        "; Composite feed: HP fraction (0-1) floor for essential/protected victims when ExcludeEssentialFromLethal is on - they drain to this floor but are never killed");

    // Integration
    ini.SetBoolValue("Integration", "EnableSacrosanct", Integration.EnableSacrosanct,
        "; Enable Sacrosanct vampire overhaul integration (auto-detects mod)");
    ini.SetBoolValue("Integration", "EnableSacrilege", Integration.EnableSacrilege,
        "; Enable Sacrilege vampire overhaul integration (auto-detects mod)");
    ini.SetBoolValue("Integration", "EnableBetterVampires", Integration.EnableBetterVampires,
        "; Enable Better Vampires integration (auto-detects mod)");
    ini.SetBoolValue("Integration", "PoiseIgnoresLevelCheck", Integration.PoiseIgnoresLevelCheck,
        "; When poise mod (ChocolatePoise/loki_POISE) is detected, ignore level requirements for feeding");
    ini.SetBoolValue("Integration", "DeepSacrosanctIntegration", Integration.DeepSacrosanctIntegration,
        "; Use C++ to mimic Sacrosanct ProcessFeed for lethal feeds (bypasses Papyrus)");
    ini.SetBoolValue("Integration", "DeepSacrilegeIntegration", Integration.DeepSacrilegeIntegration,
        "; Use C++ to mimic Sacrilege ProcessFeed for lethal feeds (bypasses Papyrus)");
    ini.SetBoolValue("Integration", "EnableSacrosanctInCombat", Integration.EnableSacrosanctInCombat,
        "; Use C++ integration for Sacrosanct during combat (bypasses AI-driven state issues)");
    ini.SetBoolValue("Integration", "EnableSacrilegeInCombat", Integration.EnableSacrilegeInCombat,
        "; Use C++ integration for Sacrilege during combat (bypasses AI-driven state issues)");
    ini.SetBoolValue("Integration", "EnableVampireFeedProxy", Integration.EnableVampireFeedProxy,
        "; When VampireFeedProxy.dll is detected, skip vanilla feed events (proxy handles them)");

    SI_Error rc = ini.SaveFile(INI_PATH);
    if (rc < 0) {
        SKSE::log::error("Failed to save INI file");
    } else {
        SKSE::log::info("INI file saved");
    }
}
