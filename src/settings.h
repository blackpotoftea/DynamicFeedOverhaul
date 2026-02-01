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
        bool ForceVampire{ false };  // Debug: skip vampire check
        bool CheckHungerStage{ false };  // Only allow feeding based on hunger stage
        int MinHungerStage{ 1 };         // Minimum hunger stage required (1-4)
        int ForceFeedType{ 0 };          // Debug: force specific FeedType (0=auto)
        bool SequentialPlay{ false }; // Debug: sequential animation test mode
    } General;

    // Non-combat feeding settings
    struct {
        bool AllowStanding{ true };
        bool AllowSleeping{ true };
        bool AllowSittingChair{ false };  // Excluded by default (no animation)
        bool EnableHeightAdjust{ true };  // Adjust actor positions on stairs
        float MinHeightDiff{ 10.0f };     // Minimum height diff to trigger adjustment
        float MaxHeightDiff{ 150.0f };    // Max height diff (~3-4 stair steps)
    } NonCombat;

    // Combat feeding settings
    struct {
        bool Enabled{ true };
        bool IgnoreHungerCheck{ true };  // Allow combat feeding even when not hungry
        bool RequireLowHealth{ false };
        float LowHealthThreshold{ 0.25f };
    } Combat;

    // Target filtering settings
    struct {
        bool EnableLevelCheck{ false };
        int MaxLevelDifference{ 10 };           // Max levels above player
        bool ExcludeInScene{ true };            // Skip actors in scenes (dialogues, scripted events)
        bool ExcludeDead{ true };               // Skip dead actors
        std::vector<std::string> IncludeKeywords;  // Only feed if has any of these keywords
        std::vector<std::string> ExcludeKeywords;  // Never feed if has any of these keywords
    } Filtering;

    // Animation selection settings
    // FeedType IDs grouped by: combat/non-combat, hungry/sated, front/back, gender
    // Selection: position-based (front/back) > gender (female > unisex fallback)
    struct {
        bool EnableRandomSelection{ true };     // Enable random animation from available list
        int HungryThreshold{ 3 };               // Hunger stage >= this uses hungry animations (1-4)

        // Non-combat sated front animations
        std::vector<int> NonCombatSatedFrontUnisex;
        std::vector<int> NonCombatSatedFrontFemale;

        // Non-combat sated back animations
        std::vector<int> NonCombatSatedBackUnisex;
        std::vector<int> NonCombatSatedBackFemale;

        // Non-combat hungry front animations
        std::vector<int> NonCombatHungryFrontUnisex;
        std::vector<int> NonCombatHungryFrontFemale;

        // Non-combat hungry back animations
        std::vector<int> NonCombatHungryBackUnisex;
        std::vector<int> NonCombatHungryBackFemale;

        // Combat sated front animations
        std::vector<int> CombatSatedFrontUnisex;
        std::vector<int> CombatSatedFrontFemale;

        // Combat sated back animations
        std::vector<int> CombatSatedBackUnisex;
        std::vector<int> CombatSatedBackFemale;

        // Combat hungry front animations
        std::vector<int> CombatHungryFrontUnisex;
        std::vector<int> CombatHungryFrontFemale;

        // Combat hungry back animations
        std::vector<int> CombatHungryBackUnisex;
        std::vector<int> CombatHungryBackFemale;
    } Animation;

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
