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
    General.ForceVampire = ini.GetBoolValue("General", "ForceVampire", General.ForceVampire);
    General.CheckHungerStage = ini.GetBoolValue("General", "CheckHungerStage", General.CheckHungerStage);
    General.MinHungerStage = static_cast<int>(ini.GetLongValue("General", "MinHungerStage", General.MinHungerStage));
    General.ForceFeedType = static_cast<int>(ini.GetLongValue("General", "ForceFeedType", General.ForceFeedType));
    General.SequentialPlay = ini.GetBoolValue("General", "SequentialPlay", General.SequentialPlay);

    // NonCombat
    NonCombat.AllowStanding = ini.GetBoolValue("NonCombat", "AllowStanding", NonCombat.AllowStanding);
    NonCombat.AllowSleeping = ini.GetBoolValue("NonCombat", "AllowSleeping", NonCombat.AllowSleeping);
    NonCombat.AllowSittingChair = ini.GetBoolValue("NonCombat", "AllowSittingChair", NonCombat.AllowSittingChair);
    NonCombat.EnableHeightAdjust = ini.GetBoolValue("NonCombat", "EnableHeightAdjust", NonCombat.EnableHeightAdjust);
    NonCombat.MinHeightDiff = static_cast<float>(ini.GetDoubleValue("NonCombat", "MinHeightDiff", NonCombat.MinHeightDiff));
    NonCombat.MaxHeightDiff = static_cast<float>(ini.GetDoubleValue("NonCombat", "MaxHeightDiff", NonCombat.MaxHeightDiff));

    // Combat
    Combat.Enabled = ini.GetBoolValue("Combat", "Enabled", Combat.Enabled);
    Combat.IgnoreHungerCheck = ini.GetBoolValue("Combat", "IgnoreHungerCheck", Combat.IgnoreHungerCheck);
    Combat.RequireLowHealth = ini.GetBoolValue("Combat", "RequireLowHealth", Combat.RequireLowHealth);
    Combat.LowHealthThreshold = static_cast<float>(ini.GetDoubleValue("Combat", "LowHealthThreshold", Combat.LowHealthThreshold));

    // Filtering
    Filtering.EnableLevelCheck = ini.GetBoolValue("Filtering", "EnableLevelCheck", Filtering.EnableLevelCheck);
    Filtering.MaxLevelDifference = static_cast<int>(ini.GetLongValue("Filtering", "MaxLevelDifference", Filtering.MaxLevelDifference));
    Filtering.ExcludeInScene = ini.GetBoolValue("Filtering", "ExcludeInScene", Filtering.ExcludeInScene);
    Filtering.ExcludeDead = ini.GetBoolValue("Filtering", "ExcludeDead", Filtering.ExcludeDead);
    Filtering.IncludeKeywords = ParseKeywordList(ini.GetValue("Filtering", "IncludeKeywords", ""));
    Filtering.ExcludeKeywords = ParseKeywordList(ini.GetValue("Filtering", "ExcludeKeywords", ""));

    // Animation
    Animation.EnableRandomSelection = ini.GetBoolValue("Animation", "EnableRandomSelection", Animation.EnableRandomSelection);
    Animation.HungryThreshold = static_cast<int>(ini.GetLongValue("Animation", "HungryThreshold", Animation.HungryThreshold));

    // Non-combat sated front
    Animation.NonCombatSatedFrontUnisex = ParseIntList(ini.GetValue("Animation", "NonCombatSatedFrontUnisex", ""));
    Animation.NonCombatSatedFrontFemale = ParseIntList(ini.GetValue("Animation", "NonCombatSatedFrontFemale", ""));

    // Non-combat sated back
    Animation.NonCombatSatedBackUnisex = ParseIntList(ini.GetValue("Animation", "NonCombatSatedBackUnisex", ""));
    Animation.NonCombatSatedBackFemale = ParseIntList(ini.GetValue("Animation", "NonCombatSatedBackFemale", ""));

    // Non-combat hungry front
    Animation.NonCombatHungryFrontUnisex = ParseIntList(ini.GetValue("Animation", "NonCombatHungryFrontUnisex", ""));
    Animation.NonCombatHungryFrontFemale = ParseIntList(ini.GetValue("Animation", "NonCombatHungryFrontFemale", ""));

    // Non-combat hungry back
    Animation.NonCombatHungryBackUnisex = ParseIntList(ini.GetValue("Animation", "NonCombatHungryBackUnisex", ""));
    Animation.NonCombatHungryBackFemale = ParseIntList(ini.GetValue("Animation", "NonCombatHungryBackFemale", ""));

    // Combat sated front
    Animation.CombatSatedFrontUnisex = ParseIntList(ini.GetValue("Animation", "CombatSatedFrontUnisex", ""));
    Animation.CombatSatedFrontFemale = ParseIntList(ini.GetValue("Animation", "CombatSatedFrontFemale", ""));

    // Combat sated back
    Animation.CombatSatedBackUnisex = ParseIntList(ini.GetValue("Animation", "CombatSatedBackUnisex", ""));
    Animation.CombatSatedBackFemale = ParseIntList(ini.GetValue("Animation", "CombatSatedBackFemale", ""));

    // Combat hungry front
    Animation.CombatHungryFrontUnisex = ParseIntList(ini.GetValue("Animation", "CombatHungryFrontUnisex", ""));
    Animation.CombatHungryFrontFemale = ParseIntList(ini.GetValue("Animation", "CombatHungryFrontFemale", ""));

    // Combat hungry back
    Animation.CombatHungryBackUnisex = ParseIntList(ini.GetValue("Animation", "CombatHungryBackUnisex", ""));
    Animation.CombatHungryBackFemale = ParseIntList(ini.GetValue("Animation", "CombatHungryBackFemale", ""));

    SKSE::log::info("Settings loaded:");
    SKSE::log::info("  [General] EnableMod={}, DebugLogging={}, ForceVampire={}, CheckHunger={} (min={}), ForceFeedType={}, SequentialPlay={}",
        General.EnableMod, General.DebugLogging, General.ForceVampire,
        General.CheckHungerStage, General.MinHungerStage, General.ForceFeedType, General.SequentialPlay);
    SKSE::log::info("  [NonCombat] Standing={}, Sleeping={}, SittingChair={}, HeightAdjust={} (min={}, max={})",
        NonCombat.AllowStanding, NonCombat.AllowSleeping, NonCombat.AllowSittingChair,
        NonCombat.EnableHeightAdjust, NonCombat.MinHeightDiff, NonCombat.MaxHeightDiff);
    SKSE::log::info("  [Combat] Enabled={}, IgnoreHungerCheck={}, RequireLowHealth={}, LowHealthThreshold={}",
        Combat.Enabled, Combat.IgnoreHungerCheck, Combat.RequireLowHealth, Combat.LowHealthThreshold);
    SKSE::log::info("  [Filtering] EnableLevelCheck={}, MaxLevelDiff={}, ExcludeInScene={}, ExcludeDead={}, IncludeKW=[{}], ExcludeKW=[{}]",
        Filtering.EnableLevelCheck, Filtering.MaxLevelDifference, Filtering.ExcludeInScene, Filtering.ExcludeDead,
        JoinKeywordList(Filtering.IncludeKeywords), JoinKeywordList(Filtering.ExcludeKeywords));
    SKSE::log::info("  [Animation] EnableRandom={}, HungryThreshold={}",
        Animation.EnableRandomSelection, Animation.HungryThreshold);
}

void Settings::SaveINI() {
    CSimpleIniA ini;
    ini.SetUnicode();

    // General
    ini.SetBoolValue("General", "EnableMod", General.EnableMod,
        "; Enable or disable the entire mod");
    ini.SetBoolValue("General", "DebugLogging", General.DebugLogging,
        "; Enable detailed debug logging");
    ini.SetBoolValue("General", "ForceVampire", General.ForceVampire,
        "; Debug: skip vampire check, always allow feeding");
    ini.SetBoolValue("General", "CheckHungerStage", General.CheckHungerStage,
        "; Only allow feeding if vampire hunger stage >= MinHungerStage");
    ini.SetLongValue("General", "MinHungerStage", General.MinHungerStage,
        "; Minimum hunger stage required to feed (1-4, where 4 is most hungry)");
    ini.SetLongValue("General", "ForceFeedType", General.ForceFeedType,
        "; Debug: force specific FeedType (0=auto, 11-14=standing, 21-24=sleeping, 31-34=sitting, 41-44=combat)");
    ini.SetBoolValue("General", "SequentialPlay", General.SequentialPlay,
        "; Debug: sequential animation test mode - plays all unisex animations for detected direction");

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

    // Combat
    ini.SetBoolValue("Combat", "Enabled", Combat.Enabled,
        "; Enable combat feeding");
    ini.SetBoolValue("Combat", "IgnoreHungerCheck", Combat.IgnoreHungerCheck,
        "; Allow combat feeding even when CheckHungerStage would block non-combat feeding");
    ini.SetBoolValue("Combat", "RequireLowHealth", Combat.RequireLowHealth,
        "; Require target to be below health threshold for combat feeding");
    ini.SetDoubleValue("Combat", "LowHealthThreshold", Combat.LowHealthThreshold,
        "; Health percentage threshold (0.0-1.0) for combat feeding");

    // Filtering
    ini.SetBoolValue("Filtering", "EnableLevelCheck", Filtering.EnableLevelCheck,
        "; Exclude targets above player level + MaxLevelDifference");
    ini.SetLongValue("Filtering", "MaxLevelDifference", Filtering.MaxLevelDifference,
        "; Max levels target can be above player (ignored if EnableLevelCheck=false)");
    ini.SetBoolValue("Filtering", "ExcludeInScene", Filtering.ExcludeInScene,
        "; Exclude targets currently in a scene (dialogues, scripted events)");
    ini.SetBoolValue("Filtering", "ExcludeDead", Filtering.ExcludeDead,
        "; Exclude dead actors from feeding");
    ini.SetValue("Filtering", "IncludeKeywords", JoinKeywordList(Filtering.IncludeKeywords).c_str(),
        "; Only allow feeding if target has ANY of these keywords (comma-separated, empty=allow all)");
    ini.SetValue("Filtering", "ExcludeKeywords", JoinKeywordList(Filtering.ExcludeKeywords).c_str(),
        "; Never allow feeding if target has ANY of these keywords (comma-separated)");

    // Animation
    ini.SetBoolValue("Animation", "EnableRandomSelection", Animation.EnableRandomSelection,
        "; Enable random animation selection from available FeedType lists");
    ini.SetLongValue("Animation", "HungryThreshold", Animation.HungryThreshold,
        "; Hunger stage >= this uses hungry animations (1=sated, 2=peckish, 3=hungry, 4=starving)");

    // Non-combat sated front
    ini.SetValue("Animation", "NonCombatSatedFrontUnisex", JoinIntList(Animation.NonCombatSatedFrontUnisex).c_str(),
        "; FeedType IDs for non-combat sated front animations (unisex)");
    ini.SetValue("Animation", "NonCombatSatedFrontFemale", JoinIntList(Animation.NonCombatSatedFrontFemale).c_str(),
        "; FeedType IDs for non-combat sated front animations (female player)");

    // Non-combat sated back
    ini.SetValue("Animation", "NonCombatSatedBackUnisex", JoinIntList(Animation.NonCombatSatedBackUnisex).c_str(),
        "; FeedType IDs for non-combat sated back animations (unisex)");
    ini.SetValue("Animation", "NonCombatSatedBackFemale", JoinIntList(Animation.NonCombatSatedBackFemale).c_str(),
        "; FeedType IDs for non-combat sated back animations (female player)");

    // Non-combat hungry front
    ini.SetValue("Animation", "NonCombatHungryFrontUnisex", JoinIntList(Animation.NonCombatHungryFrontUnisex).c_str(),
        "; FeedType IDs for non-combat hungry front animations (unisex)");
    ini.SetValue("Animation", "NonCombatHungryFrontFemale", JoinIntList(Animation.NonCombatHungryFrontFemale).c_str(),
        "; FeedType IDs for non-combat hungry front animations (female player)");

    // Non-combat hungry back
    ini.SetValue("Animation", "NonCombatHungryBackUnisex", JoinIntList(Animation.NonCombatHungryBackUnisex).c_str(),
        "; FeedType IDs for non-combat hungry back animations (unisex)");
    ini.SetValue("Animation", "NonCombatHungryBackFemale", JoinIntList(Animation.NonCombatHungryBackFemale).c_str(),
        "; FeedType IDs for non-combat hungry back animations (female player)");

    // Combat sated front
    ini.SetValue("Animation", "CombatSatedFrontUnisex", JoinIntList(Animation.CombatSatedFrontUnisex).c_str(),
        "; FeedType IDs for combat sated front animations (unisex)");
    ini.SetValue("Animation", "CombatSatedFrontFemale", JoinIntList(Animation.CombatSatedFrontFemale).c_str(),
        "; FeedType IDs for combat sated front animations (female player)");

    // Combat sated back
    ini.SetValue("Animation", "CombatSatedBackUnisex", JoinIntList(Animation.CombatSatedBackUnisex).c_str(),
        "; FeedType IDs for combat sated back animations (unisex)");
    ini.SetValue("Animation", "CombatSatedBackFemale", JoinIntList(Animation.CombatSatedBackFemale).c_str(),
        "; FeedType IDs for combat sated back animations (female player)");

    // Combat hungry front
    ini.SetValue("Animation", "CombatHungryFrontUnisex", JoinIntList(Animation.CombatHungryFrontUnisex).c_str(),
        "; FeedType IDs for combat hungry front animations (unisex)");
    ini.SetValue("Animation", "CombatHungryFrontFemale", JoinIntList(Animation.CombatHungryFrontFemale).c_str(),
        "; FeedType IDs for combat hungry front animations (female player)");

    // Combat hungry back
    ini.SetValue("Animation", "CombatHungryBackUnisex", JoinIntList(Animation.CombatHungryBackUnisex).c_str(),
        "; FeedType IDs for combat hungry back animations (unisex)");
    ini.SetValue("Animation", "CombatHungryBackFemale", JoinIntList(Animation.CombatHungryBackFemale).c_str(),
        "; FeedType IDs for combat hungry back animations (female player)");

    SI_Error rc = ini.SaveFile(INI_PATH);
    if (rc < 0) {
        SKSE::log::error("Failed to save INI file");
    } else {
        SKSE::log::info("INI file saved");
    }
}
