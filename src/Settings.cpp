#include "PCH.h"
#include "Settings.h"
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
    General.DebugLogging = ini.GetBoolValue("General", "DebugLogging", General.DebugLogging);
    General.EnableWerewolf = ini.GetBoolValue("General", "EnableWerewolf", General.EnableWerewolf);
    General.EnableVampireLord = ini.GetBoolValue("General", "EnableVampireLord", General.EnableVampireLord);

    // Update log level based on INI setting
    if (General.DebugLogging) {
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
    } else {
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }

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

    // PromptDisplay
    PromptDisplay.RequireWeaponDrawn = ini.GetBoolValue("PromptDisplay", "RequireWeaponDrawn", PromptDisplay.RequireWeaponDrawn);
    PromptDisplay.ShowWhenSneaking = ini.GetBoolValue("PromptDisplay", "ShowWhenSneaking", PromptDisplay.ShowWhenSneaking);
    PromptDisplay.RequirePlayerFacing = ini.GetBoolValue("PromptDisplay", "RequirePlayerFacing", PromptDisplay.RequirePlayerFacing);
    PromptDisplay.FacingAngleThreshold = static_cast<float>(ini.GetDoubleValue("PromptDisplay", "FacingAngleThreshold", PromptDisplay.FacingAngleThreshold));

    // NonCombat
    NonCombat.AllowStanding = ini.GetBoolValue("NonCombat", "AllowStanding", NonCombat.AllowStanding);
    NonCombat.AllowSleeping = ini.GetBoolValue("NonCombat", "AllowSleeping", NonCombat.AllowSleeping);
    NonCombat.AllowSittingChair = ini.GetBoolValue("NonCombat", "AllowSittingChair", NonCombat.AllowSittingChair);
    NonCombat.EnableHeightAdjust = ini.GetBoolValue("NonCombat", "EnableHeightAdjust", NonCombat.EnableHeightAdjust);
    NonCombat.MinHeightDiff = static_cast<float>(ini.GetDoubleValue("NonCombat", "MinHeightDiff", NonCombat.MinHeightDiff));
    NonCombat.MaxHeightDiff = static_cast<float>(ini.GetDoubleValue("NonCombat", "MaxHeightDiff", NonCombat.MaxHeightDiff));
    NonCombat.UseTwoSingleAnimations = ini.GetBoolValue("NonCombat", "UseTwoSingleAnimations", NonCombat.UseTwoSingleAnimations);
    NonCombat.PlayerStandingFrontAnim = ini.GetValue("NonCombat", "PlayerStandingFrontAnim", NonCombat.PlayerStandingFrontAnim.c_str());
    NonCombat.TargetStandingFrontAnim = ini.GetValue("NonCombat", "TargetStandingFrontAnim", NonCombat.TargetStandingFrontAnim.c_str());
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

    // Animation
    Animation.EnableRandomSelection = ini.GetBoolValue("Animation", "EnableRandomSelection", Animation.EnableRandomSelection);
    Animation.HungryThreshold = static_cast<int>(ini.GetLongValue("Animation", "HungryThreshold", Animation.HungryThreshold));
    Animation.EnableTimeSlowdown = ini.GetBoolValue("Animation", "EnableTimeSlowdown", Animation.EnableTimeSlowdown);
    Animation.TimeSlowdownMultiplier = static_cast<float>(ini.GetDoubleValue("Animation", "TimeSlowdownMultiplier", Animation.TimeSlowdownMultiplier));

    // Integration
    Integration.EnableSacrosanct = ini.GetBoolValue("Integration", "EnableSacrosanct", Integration.EnableSacrosanct);
    Integration.EnableSacrilege = ini.GetBoolValue("Integration", "EnableSacrilege", Integration.EnableSacrilege);
    Integration.EnableBetterVampires = ini.GetBoolValue("Integration", "EnableBetterVampires", Integration.EnableBetterVampires);
    Integration.PoiseIgnoresLevelCheck = ini.GetBoolValue("Integration", "PoiseIgnoresLevelCheck", Integration.PoiseIgnoresLevelCheck);
    Integration.DisableSacrosanctInCombat = ini.GetBoolValue("Integration", "DisableSacrosanctInCombat", Integration.DisableSacrosanctInCombat);

    SKSE::log::info("Settings loaded:");
    SKSE::log::info("  [General] EnableMod={}, DebugLogging={}, Werewolf={}, VL={}, ForceVampire={}, CheckHunger={} (min={}), ForceFeedType={}, DebugAnimationCycle={}, AnimationTimeout={}, PeriodicCheckInterval={}, PromptDelaySeconds={}",
        General.EnableMod, General.DebugLogging, General.EnableWerewolf, General.EnableVampireLord, General.ForceVampire,
        General.CheckHungerStage, General.MinHungerStage, General.ForceFeedType, General.DebugAnimationCycle, General.AnimationTimeout, General.PeriodicCheckInterval, General.PromptDelayIdleSeconds);
    SKSE::log::info("  [Input] FeedKey=0x{:X}, FeedGamepadKey=0x{:X}", Input.FeedKey, Input.FeedGamepadKey);
    SKSE::log::info("  [PromptDisplay] RequireWeaponDrawn={}, ShowWhenSneaking={}, RequirePlayerFacing={}, FacingAngleThreshold={}",
        PromptDisplay.RequireWeaponDrawn, PromptDisplay.ShowWhenSneaking, PromptDisplay.RequirePlayerFacing, PromptDisplay.FacingAngleThreshold);
    SKSE::log::info("  [NonCombat] Standing={}, Sleeping={}, SittingChair={}, HeightAdjust={} (min={}, max={}), TwoSingle={}, EnableLethalFeed={}, LethalHoldDuration={}, ExcludeEssentialFromLethal={}, EnableLevelCheck={}, MaxLevelDiff={}",
        NonCombat.AllowStanding, NonCombat.AllowSleeping, NonCombat.AllowSittingChair,
        NonCombat.EnableHeightAdjust, NonCombat.MinHeightDiff, NonCombat.MaxHeightDiff,
        NonCombat.UseTwoSingleAnimations, NonCombat.EnableLethalFeed, NonCombat.LethalHoldDuration, NonCombat.ExcludeEssentialFromLethal,
        NonCombat.EnableLevelCheck, NonCombat.MaxLevelDifference);
    if (NonCombat.UseTwoSingleAnimations) {
        SKSE::log::info("  [NonCombat] PlayerAnim='{}', TargetAnim='{}'",
            NonCombat.PlayerStandingFrontAnim, NonCombat.TargetStandingFrontAnim);
    }
    SKSE::log::info("  [Combat] Enabled={}, IgnoreHungerCheck={}, RequireLowHealth={}, LowHealthThreshold={}, AllowStaggered={}, StaggerRequireLowerLevel={}, StaggerMaxLevelDiff={}, WitnessDetection={}, WitnessRadius={}, WitnessInterval={}, WitnessDebugLog={}, PromptDelay={}",
        Combat.Enabled, Combat.IgnoreHungerCheck, Combat.RequireLowHealth, Combat.LowHealthThreshold, Combat.AllowStaggered,
        Combat.StaggerRequireLowerLevel, Combat.StaggerMaxLevelDifference,
        Combat.EnableWitnessDetection, Combat.WitnessDetectionRadius, Combat.WitnessCheckInterval, Combat.WitnessDebugLogging, Combat.PromptDelayCombatSeconds);
    SKSE::log::info("  [Filtering] ExcludeInScene={}, ExcludeOStim={}, ExcludeDead={}, AllowRecentlyDead={}, MaxDeadHours={}, MaxDeadFeeds={}, IncludeKW=[{}], ExcludeKW=[{}], ExcludeActorIDs=[{}]",
        Filtering.ExcludeInScene, Filtering.ExcludeOStimScenes, Filtering.ExcludeDead,
        Filtering.AllowRecentlyDead, Filtering.MaxDeadHours, Filtering.MaxDeadFeeds,
        JoinKeywordList(Filtering.IncludeKeywords), JoinKeywordList(Filtering.ExcludeKeywords), JoinKeywordList(Filtering.ExcludeActorIDs));
    SKSE::log::info("  [Animation] EnableRandom={}, HungryThreshold={}, EnableTimeSlowdown={}, TimeSlowdownMultiplier={}",
        Animation.EnableRandomSelection, Animation.HungryThreshold, Animation.EnableTimeSlowdown, Animation.TimeSlowdownMultiplier);
    SKSE::log::info("  [Integration] EnableSacrosanct={}, EnableSacrilege={}, EnableBetterVampires={}, PoiseIgnoresLevelCheck={}, DisableSacrosanctInCombat={}",
        Integration.EnableSacrosanct, Integration.EnableSacrilege, Integration.EnableBetterVampires, Integration.PoiseIgnoresLevelCheck, Integration.DisableSacrosanctInCombat);
}

void Settings::SaveINI() {
    CSimpleIniA ini;
    ini.SetUnicode();

    // General
    ini.SetBoolValue("General", "EnableMod", General.EnableMod,
        "; Enable or disable the entire mod");
    ini.SetBoolValue("General", "DebugLogging", General.DebugLogging,
        "; Enable detailed debug logging");
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
        "; Keyboard key code for feed prompt (default 0x22 = G)");
    ini.SetLongValue("Input", "FeedGamepadKey", Input.FeedGamepadKey,
        "; Gamepad key code for feed prompt (default 0x1000 = A)");

    // PromptDisplay
    ini.SetBoolValue("PromptDisplay", "RequireWeaponDrawn", PromptDisplay.RequireWeaponDrawn,
        "; Only show feed prompt when weapon/magic is drawn or player is in combat");
    ini.SetBoolValue("PromptDisplay", "ShowWhenSneaking", PromptDisplay.ShowWhenSneaking,
        "; Show feed prompt when player is sneaking (enables stealth takedowns via vampire feed)");
    ini.SetBoolValue("PromptDisplay", "RequirePlayerFacing", PromptDisplay.RequirePlayerFacing,
        "; Only show feed prompt when player is facing the target");
    ini.SetDoubleValue("PromptDisplay", "FacingAngleThreshold", PromptDisplay.FacingAngleThreshold,
        "; Maximum angle (degrees) from player heading to target (90 = 180 degree cone in front)");

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
    ini.SetBoolValue("NonCombat", "UseTwoSingleAnimations", NonCombat.UseTwoSingleAnimations,
        "; Use two single-actor animations instead of paired animations (requires custom animations)");
    ini.SetValue("NonCombat", "PlayerStandingFrontAnim", NonCombat.PlayerStandingFrontAnim.c_str(),
        "; Animation event name for player in two-single standing front feed");
    ini.SetValue("NonCombat", "TargetStandingFrontAnim", NonCombat.TargetStandingFrontAnim.c_str(),
        "; Animation event name for target in two-single standing front feed");
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

    // Animation
    ini.SetBoolValue("Animation", "EnableRandomSelection", Animation.EnableRandomSelection,
        "; Enable random animation selection from available FeedType lists");
    ini.SetLongValue("Animation", "HungryThreshold", Animation.HungryThreshold,
        "; Hunger stage >= this uses hungry animations (1=sated, 2=peckish, 3=hungry, 4=starving)");
    ini.SetBoolValue("Animation", "EnableTimeSlowdown", Animation.EnableTimeSlowdown,
        "; Enable time slowdown effect when paired feed animation starts");
    ini.SetDoubleValue("Animation", "TimeSlowdownMultiplier", Animation.TimeSlowdownMultiplier,
        "; Time multiplier during feed (0.4 = 40% speed, 1.0 = normal speed)");

    // Integration
    ini.SetBoolValue("Integration", "EnableSacrosanct", Integration.EnableSacrosanct,
        "; Enable Sacrosanct vampire overhaul integration (auto-detects mod)");
    ini.SetBoolValue("Integration", "EnableSacrilege", Integration.EnableSacrilege,
        "; Enable Sacrilege vampire overhaul integration (auto-detects mod)");
    ini.SetBoolValue("Integration", "EnableBetterVampires", Integration.EnableBetterVampires,
        "; Enable Better Vampires integration (auto-detects mod)");
    ini.SetBoolValue("Integration", "PoiseIgnoresLevelCheck", Integration.PoiseIgnoresLevelCheck,
        "; When poise mod (ChocolatePoise/loki_POISE) is detected, ignore level requirements for feeding");
    ini.SetBoolValue("Integration", "DisableSacrosanctInCombat", Integration.DisableSacrosanctInCombat,
        "; Skip Sacrosanct/Sacrilege Papyrus calls during combat (Papyrus is unreliable in combat)");

    SI_Error rc = ini.SaveFile(INI_PATH);
    if (rc < 0) {
        SKSE::log::error("Failed to save INI file");
    } else {
        SKSE::log::info("INI file saved");
    }
}
