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
        bool EnableWerewolf{ false };     // Enable for Werewolf form
        bool EnableVampireLord{ true };  // Enable for Vampire Lord form
        bool ForceVampire{ false };  // Debug: skip vampire check
        bool CheckHungerStage{ false };  // Only allow feeding based on hunger stage
        int MinHungerStage{ 1 };         // Minimum hunger stage required (1-4)
        int ForceFeedType{ 0 };          // Debug: force specific FeedType (0=auto)
        bool DebugAnimationCycle{ false }; // Debug: cycle through all animations sequentially
        float AnimationTimeout{ 15.0f }; // Timeout for animation events in seconds
        float PeriodicCheckInterval{ 1.0f }; // Interval for periodic checks
        float PromptDelaySeconds{ 0.2f }; // Delay before showing prompt when targeting new NPC
    } General;

    // Input settings
    struct {
        int FeedKey{ 0x22 };          // Keyboard G
        int FeedGamepadKey{ 0x1000 }; // Gamepad A
    } Input;

    // Prompt Display settings
    struct {
        bool RequireWeaponDrawn{ false };  // Only show prompt when weapon/magic drawn or in combat
        bool ShowWhenSneaking{ true };     // Show prompt when sneaking (enables stealth takedowns)
        bool RequirePlayerFacing{ true };  // Only show prompt when player is facing target
        float FacingAngleThreshold{ 90.0f };  // Max angle (degrees) from player heading to target
    } PromptDisplay;

    // Non-combat feeding settings
    struct {
        bool AllowStanding{ true };
        bool AllowSleeping{ true };
        bool AllowSittingChair{ false };  // Excluded by default (no animation)
        bool EnableHeightAdjust{ true };  // Adjust actor positions on stairs
        float MinHeightDiff{ 10.0f };     // Minimum height diff to trigger adjustment
        float MaxHeightDiff{ 150.0f };    // Max height diff (~3-4 stair steps)
        bool UseTwoSingleAnimations{ true };  // Use two single-actor animations instead of paired
        std::string PlayerStandingFrontAnim{ "VampireFeed_Player_StandingFront" };  // Two-single player animation
        std::string TargetStandingFrontAnim{ "VampireFeed_Target_StandingFront" };  // Two-single target animation
        float TargetOffsetX{ 0.0f };   // Target X offset from player (local coords)
        float TargetOffsetY{ 100.0f }; // Target Y offset (positive = in front)
        float TargetOffsetZ{ 0.0f };   // Target Z offset (height)
        bool EnableLethalFeed{ false };      // Enable hold-to-kill feature for non-combat targets
        float LethalHoldDuration{ 5.0f };    // Seconds to hold button for lethal feed
        bool ExcludeEssentialFromLethal{ true };  // Don't show kill prompt for Essential actors
        bool EnableRotation{ true };         // Rotate player/target to face each other before feed
        bool EnableLevelCheck{ false };      // Exclude targets above player level (non-combat only)
        int MaxLevelDifference{ 10 };        // Max levels above player to allow feeding
    } NonCombat;

    // Combat feeding settings
    struct {
        bool Enabled{ true };
        bool IgnoreHungerCheck{ true };  // Allow combat feeding even when not hungry
        bool RequireLowHealth{ false };
        float LowHealthThreshold{ 0.25f };
        bool AllowStaggered{ true };     // Allow feeding on staggered targets (bypasses health check)
        bool StaggerRequireLowerLevel{ true };   // Stagger feeding requires target to be lower level than player
        int StaggerMaxLevelDifference{ 10 };     // Target must be (playerLevel - this) or lower (e.g. player 20, diff 10 = target max 10)
        bool EnableWitnessDetection{ true };     // Stop feed if witnessed by NPCs
        float WitnessDetectionRadius{ 1500.0f }; // Detection radius in units (~1500 = reasonable distance)
        float WitnessCheckInterval{ 0.5f };      // How often to check for witnesses during feed (seconds)
        bool WitnessDebugLogging{ false };       // Enable verbose witness detection logging
    } Combat;

    // Target filtering settings
    struct {
        bool ExcludeInScene{ true };            // Skip actors in scenes (dialogues, scripted events)
        bool ExcludeOStimScenes{ true };        // Skip actors in OStim scenes (requires OStim NG)
        bool ExcludeDead{ true };               // Skip dead actors
        bool AllowRecentlyDead{ false };        // Allow feeding on recently dead actors
        float MaxDeadHours{ 1.0f };             // Maximum hours since death to allow feeding
        int MaxDeadFeeds{ 1 };                  // Maximum times to feed on a single corpse (0=unlimited)
        std::vector<std::string> IncludeKeywords;  // Only feed if has any of these keywords
        std::vector<std::string> ExcludeKeywords;  // Never feed if has any of these keywords
        std::vector<std::string> ExcludeActorIDs;  // Never feed on specific NPC base IDs (format: PluginName|0xFormID)
    } Filtering;

    // Icon overlay settings
    struct {
        bool EnableIconOverlay{ true };          // Show icon above target's head during feed
        int IconPosition{ 0 };                    // 0=AboveHead, 1=RightOfHead
        float IconDuration{ 5.0f };              // How long to display icon (seconds)
        float IconSize{ 64.0f };                 // Size of the icon
        float IconHeightOffset{ 15.0f };         // Height offset above head (game units)
        std::string IconPath{ "Data\\Interface\\ImGuiIcons\\Icons\\vampireFang.png" }; // Path to icon file
    } IconOverlay;

    // Animation selection settings
    struct {
        bool EnableRandomSelection{ true };     // Enable random animation from available list
        int HungryThreshold{ 3 };               // Hunger stage >= this uses hungry animations (1-4)
        bool EnableTimeSlowdown{ true };        // Enable time slowdown when paired feed starts
        float TimeSlowdownMultiplier{ 0.6f };   // Time multiplier during feed (0.4 = 40% speed)
    } Animation;

    // Integration settings
    struct {
        bool EnableSacrosanct{ true };          // Enable Sacrosanct integration
        bool EnableBetterVampires{ true };      // Enable Better Vampires integration
        bool PoiseIgnoresLevelCheck{ true };    // When poise mod detected, ignore level requirements
    } Integration;

    void LoadINI();
    void SaveINI();

private:
    Settings() = default;
    Settings(const Settings&) = delete;
    Settings(Settings&&) = delete;
    ~Settings() = default;

    Settings& operator=(const Settings&) = delete;
    Settings& operator=(Settings&&) = delete;

    static constexpr const wchar_t* INI_PATH = L"Data/SKSE/Plugins/DynamicFeedOverhaul.ini";
};
