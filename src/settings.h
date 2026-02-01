#pragma once

#include <SimpleIni.h>
#include <vector>
#include <string>

class Settings {
public:
    [[nodiscard]] static Settings* GetSingleton() {
        static Settings singleton;
        return &singleton;
    }

    // General settings
    struct {
        bool EnableMod{ true };
        bool DebugLogging{ false };
    } General;

    // Non-combat feeding settings
    struct {
        bool AllowStanding{ true };
        bool AllowSleeping{ true };
        bool AllowSittingChair{ false };  // Excluded by default (no animation)
    } NonCombat;

    // Combat feeding settings
    struct {
        bool Enabled{ true };
        bool RequireLowHealth{ false };
        float LowHealthThreshold{ 0.25f };
    } Combat;

    // Target filtering settings
    struct {
        bool EnableLevelCheck{ false };
        int MaxLevelDifference{ 10 };           // Max levels above player
        bool ExcludeInScene{ true };            // Skip actors in scenes (dialogues, scripted events)
        std::vector<std::string> IncludeKeywords;  // Only feed if has any of these keywords
        std::vector<std::string> ExcludeKeywords;  // Never feed if has any of these keywords
    } Filtering;

    void LoadINI();
    void SaveINI();

private:
    Settings() = default;
    Settings(const Settings&) = delete;
    Settings(Settings&&) = delete;
    ~Settings() = default;

    Settings& operator=(const Settings&) = delete;
    Settings& operator=(Settings&&) = delete;

    static constexpr const wchar_t* INI_PATH = L"Data/SKSE/Plugins/SkyPromptVampireFeed.ini";
};
