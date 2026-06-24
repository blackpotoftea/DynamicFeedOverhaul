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
        float PromptDelayIdleSeconds{ 0.2f }; // Delay before showing prompt when targeting new NPC (non-combat)
    } General;

    // Input settings
    struct {
        int FeedKey{ 0x22 };              // Keyboard G (primary)
        int FeedGamepadKey{ 0x1000 };     // Gamepad A (primary)
        int SecondaryKey{ 0x23 };         // Keyboard H (secondary prompt - Embrace for Sacrosanct)
        int SecondaryGamepadKey{ 0x2000 }; // Gamepad B (secondary prompt - Embrace for Sacrosanct)
    } Input;

    // Prompt Display settings
    struct {
        bool RequireWeaponDrawn{ false };  // Only show prompt when weapon/magic drawn or in combat
        bool ShowWhenSneaking{ true };     // Show prompt when sneaking (enables stealth takedowns)
        bool RequirePlayerFacing{ true };  // Only show prompt when player is facing target
        float FacingAngleThreshold{ 90.0f };  // Max angle (degrees) from player heading to target
        bool RelaxedCombatTargeting{ true };  // Disable facing requirement during combat
        float MaxTargetDistance{ 150.0f };    // Maximum distance to target for prompt to show (units)
    } PromptDisplay;

    // Non-combat feeding settings
    struct {
        bool AllowStanding{ true };
        bool AllowSleeping{ true };
        bool AllowSittingChair{ false };  // Excluded by default (no animation)
        bool EnableHeightAdjust{ true };  // Adjust actor positions on stairs
        float MinHeightDiff{ 10.0f };     // Minimum height diff to trigger adjustment
        float MaxHeightDiff{ 150.0f };    // Max height diff (~3-4 stair steps)
        bool UseCompositePairedAnimation{ true };  // Composite two single-actor animations to simulate a paired animation
        bool UseCompositeFurnitureAnimation{ true };  // Use player-only composite packs for bed/bedroll feeds (victim stays in furniture)
        std::string PlayerStandingFrontAnim{ "Anub2PVampireFemaleIntro_0" };  // Player-side single-actor animation (intro/loop fallback)
        std::string TargetStandingFrontAnim{ "Anub2PVampireFemaleIntro_1" };  // Target-side single-actor animation (intro/loop fallback)
        // Staged composite timing (hybrid: timer fallback; clip annotation events override)
        float CompositeIntroDuration{ 2.0f };    // Seconds Intro plays before -> Loop (fallback if no VFD_IntroEnd)
        float CompositeExitDuration{ 2.0f };     // Seconds Exit (GoBack) plays before -> Drained (fallback if no VFD_GoBackEnd)
        // Drained idle aftermath: a random length in [Min, Max] is rolled each feed.
        // Max can go up to the full Drained clip length (~9.3s) to play it in full; 0 skips it.
        float CompositeDrainedDurationMin{ 0.0f };
        float CompositeDrainedDurationMax{ 2.5f };
        float TargetOffsetX{ 0.0f };   // Target X offset from player (local coords)
        float TargetOffsetY{ 25.0f };  // Target Y offset (positive = in front). ~20-30 matches Anub2P / OStim "standing apart" choreography.
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
        float PromptDelayCombatSeconds{ 0.0f };  // Delay before showing prompt in combat (default 0 for immediate)
    } Combat;

    // Target filtering settings
    struct {
        bool ExcludeInScene{ true };            // Skip actors in scenes (dialogues, scripted events)
        bool ExcludeOStimScenes{ true };        // Skip actors in OStim scenes (requires OStim NG)
        bool ExcludeDead{ true };               // Skip dead actors
        bool AllowRecentlyDead{ false };        // Allow feeding on recently dead actors
        float MaxDeadHours{ 2.0f };             // Maximum hours since death to allow feeding
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
        std::string FailureIconPath{ "Data\\Interface\\ImGuiIcons\\Icons\\vampireFangs_fail.png" }; // Icon shown on PlayIdle failure
    } IconOverlay;

    // Victim health bar overlay (custom ImGui bar drawn above the victim during a feed)
    struct {
        bool Enable{ true };           // Show the victim's health bar during feeds
        float Width{ 120.0f };         // Bar width in pixels (before Scale)
        float Height{ 10.0f };         // Bar height in pixels (before Scale)
        float Scale{ 1.0f };           // Size multiplier applied to width and height
        float HeightOffset{ 25.0f };   // Height above the victim's head (game units)
        float OffsetX{ 0.0f };         // Screen-space horizontal offset (pixels, + = right)
        float OffsetY{ 0.0f };         // Screen-space vertical offset (pixels, + = down)
        // Trailing "damage chip" layer: a lagging bar behind the front bar that holds for a
        // beat after a hit, then slides down to catch up (shows how much was just drained).
        bool EnableTrailing{ true };   // Show the trailing layer
        float TrailingDelay{ 0.35f };  // Seconds the trailing layer holds before catching up
        float TrailingSpeed{ 1.0f };   // Catch-up speed (bar fractions per second)
    } HealthBarOverlay;

    // Animation selection settings
    struct {
        bool EnableRandomSelection{ true };     // Enable random animation from available list
        int HungryThreshold{ 3 };               // Hunger stage >= this uses hungry animations (1-4)
        bool EnableTimeSlowdown{ true };        // Enable time slowdown when paired feed starts
        float TimeSlowdownMultiplier{ 0.6f };   // Time multiplier during feed (0.4 = 40% speed)
        std::string FailureSoundForm{ "Skyrim.esm|0x3C73C" };   // Sound played at player on PlayIdle failure. Format: PluginName|0xFormID. Empty = disabled. Default: WPNBlockBlade1HandVsOtherSD (sword-parry clang).
    } Animation;

    // Health drain settings - applies on VFD_VampireFeedTrigger animation event
    // Visual HP-bar drain that floors at 1 HP (paired animation handles the kill).
    struct {
        bool Enable{ true };
        bool FloorTargetAtOneHP{ true };        // Target-only: floor drained HP at 1 so the drain itself can't kill the NPC (paired animation handles the kill). Player drain always floors at 1 regardless.
        bool DrainOnNPC{ true };                // Apply drain to the target NPC
        float LethalChunkMinPercent{ 20.0f };   // Lower bound of % current-HP drained per trigger (lethal feeds)
        float LethalChunkMaxPercent{ 50.0f };   // Upper bound (lethal feeds)
        float EscalationPerTrigger{ 1.2f };     // Multiplier on the roll for each successive trigger in same feed
        float NonLethalChunkPercent{ 10.0f };   // Fixed % per trigger on non-lethal feeds
        float MaxChunkCapPercent{ 95.0f };      // Safety cap so escalation can't one-shot to 1
        // Composite feed "gulp" drain: the feeding loop removes a randomized chunk of HP at a
        // randomized interval (like sucking blood in mouthfuls) rather than a smooth bleed.
        float GulpIntervalMin{ 0.7f };          // Min seconds between gulps
        float GulpIntervalMax{ 1.5f };          // Max seconds between gulps
        float GulpPercentMin{ 7.0f };           // Min HP drained per gulp (% of max)
        float GulpPercentMax{ 16.0f };          // Max HP drained per gulp (% of max)
        float GulpLethalThreshold{ 0.05f };     // Victim HP fraction (0-1) at/below which Loop -> Kill (drained dry)
    } HealthDrain;

    // Integration settings
    struct {
        bool EnableSacrosanct{ true };              // Enable Sacrosanct integration
        bool EnableSacrilege{ true };               // Enable Sacrilege integration
        bool EnableBetterVampires{ true };          // Enable Better Vampires integration
        bool PoiseIgnoresLevelCheck{ true };        // When poise mod detected, ignore level requirements
        bool DeepSacrosanctIntegration{ true };     // Use C++ to mimic Sacrosanct ProcessFeed (bypasses Papyrus)
        bool DeepSacrilegeIntegration{ true };      // Use C++ to mimic Sacrilege ProcessFeed (bypasses Papyrus)
        bool EnableSacrosanctInCombat{ true };      // Use C++ integration for Sacrosanct during combat
        bool EnableSacrilegeInCombat{ true };       // Use C++ integration for Sacrilege during combat
        bool EnableVampireFeedProxy{ true };        // Skip vanilla feed events when VampireFeedProxy.dll is detected
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
