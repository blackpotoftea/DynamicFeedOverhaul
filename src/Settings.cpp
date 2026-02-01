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

    // NonCombat
    NonCombat.AllowStanding = ini.GetBoolValue("NonCombat", "AllowStanding", NonCombat.AllowStanding);
    NonCombat.AllowSleeping = ini.GetBoolValue("NonCombat", "AllowSleeping", NonCombat.AllowSleeping);
    NonCombat.AllowSittingChair = ini.GetBoolValue("NonCombat", "AllowSittingChair", NonCombat.AllowSittingChair);
    NonCombat.EnableHeightAdjust = ini.GetBoolValue("NonCombat", "EnableHeightAdjust", NonCombat.EnableHeightAdjust);
    NonCombat.MinHeightDiff = static_cast<float>(ini.GetDoubleValue("NonCombat", "MinHeightDiff", NonCombat.MinHeightDiff));
    NonCombat.MaxHeightDiff = static_cast<float>(ini.GetDoubleValue("NonCombat", "MaxHeightDiff", NonCombat.MaxHeightDiff));

    // Combat
    Combat.Enabled = ini.GetBoolValue("Combat", "Enabled", Combat.Enabled);
    Combat.RequireLowHealth = ini.GetBoolValue("Combat", "RequireLowHealth", Combat.RequireLowHealth);
    Combat.LowHealthThreshold = static_cast<float>(ini.GetDoubleValue("Combat", "LowHealthThreshold", Combat.LowHealthThreshold));

    // Filtering
    Filtering.EnableLevelCheck = ini.GetBoolValue("Filtering", "EnableLevelCheck", Filtering.EnableLevelCheck);
    Filtering.MaxLevelDifference = static_cast<int>(ini.GetLongValue("Filtering", "MaxLevelDifference", Filtering.MaxLevelDifference));
    Filtering.ExcludeInScene = ini.GetBoolValue("Filtering", "ExcludeInScene", Filtering.ExcludeInScene);
    Filtering.IncludeKeywords = ParseKeywordList(ini.GetValue("Filtering", "IncludeKeywords", ""));
    Filtering.ExcludeKeywords = ParseKeywordList(ini.GetValue("Filtering", "ExcludeKeywords", ""));

    SKSE::log::info("Settings loaded:");
    SKSE::log::info("  [General] EnableMod={}, DebugLogging={}", General.EnableMod, General.DebugLogging);
    SKSE::log::info("  [NonCombat] Standing={}, Sleeping={}, SittingChair={}, HeightAdjust={} (min={}, max={})",
        NonCombat.AllowStanding, NonCombat.AllowSleeping, NonCombat.AllowSittingChair,
        NonCombat.EnableHeightAdjust, NonCombat.MinHeightDiff, NonCombat.MaxHeightDiff);
    SKSE::log::info("  [Combat] Enabled={}, RequireLowHealth={}, LowHealthThreshold={}",
        Combat.Enabled, Combat.RequireLowHealth, Combat.LowHealthThreshold);
    SKSE::log::info("  [Filtering] EnableLevelCheck={}, MaxLevelDiff={}, ExcludeInScene={}, IncludeKW=[{}], ExcludeKW=[{}]",
        Filtering.EnableLevelCheck, Filtering.MaxLevelDifference, Filtering.ExcludeInScene,
        JoinKeywordList(Filtering.IncludeKeywords), JoinKeywordList(Filtering.ExcludeKeywords));
}

void Settings::SaveINI() {
    CSimpleIniA ini;
    ini.SetUnicode();

    // General
    ini.SetBoolValue("General", "EnableMod", General.EnableMod,
        "; Enable or disable the entire mod");
    ini.SetBoolValue("General", "DebugLogging", General.DebugLogging,
        "; Enable detailed debug logging");

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
    ini.SetValue("Filtering", "IncludeKeywords", JoinKeywordList(Filtering.IncludeKeywords).c_str(),
        "; Only allow feeding if target has ANY of these keywords (comma-separated, empty=allow all)");
    ini.SetValue("Filtering", "ExcludeKeywords", JoinKeywordList(Filtering.ExcludeKeywords).c_str(),
        "; Never allow feeding if target has ANY of these keywords (comma-separated)");

    SI_Error rc = ini.SaveFile(INI_PATH);
    if (rc < 0) {
        SKSE::log::error("Failed to save INI file");
    } else {
        SKSE::log::info("INI file saved");
    }
}
