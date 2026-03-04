#pragma once

#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <cmath>

#define _USE_MATH_DEFINES
#include <math.h>

namespace AnimUtil {
    // Target state constants for feed type calculation
    // These represent base values multiplied by 10 in OAR graph variable conditions
    // Format: (TargetState * 10) + VampireHungerStage
    constexpr int kStanding = 10;
    constexpr int kSleeping = 20;
    constexpr int kSitting = 30;
    constexpr int kCombat = 40;
    constexpr int kDead = 50;

    // Idle EditorIDs
    namespace Idles {
        // Standing
        inline constexpr const char* VAMPIRE_STANDING_FRONT = "pa_1HMKillMoveShortA";
        inline constexpr const char* VAMPIRE_STANDING_BACK = "pa_1HMSneakKillBackA";
        // inline constexpr const char* VAMPIRE_STANDING_FRONT = "IdleVampireStandingFront";
        // inline constexpr const char* VAMPIRE_STANDING_BACK = "IdleVampireStandingBack";
        // Bed
        inline constexpr const char* VAMPIRE_BED_LEFT = "VampireFeedingBedLeft_Loose";
        inline constexpr const char* VAMPIRE_BED_RIGHT = "VampireFeedingBedRight_Loose";
        // Bedroll
        inline constexpr const char* VAMPIRE_BEDROLL_LEFT = "VampireFeedingBedRollLeft_Loose";
        inline constexpr const char* VAMPIRE_BEDROLL_RIGHT = "VampireFeedingBedRollRight_Loose";
        // Sitting
        inline constexpr const char* VAMPIRE_SITTING_FRONT = "VampireFeedSittingFront";
        inline constexpr const char* VAMPIRE_SITTING_BACK = "VampireFeedSittingBack";

        // Vampire Lord Standing
        inline constexpr const char* VAMPIRELORD_STANDING_FRONT = "VampireLordLeftPowerAttackFeedFront";
        inline constexpr const char* VAMPIRELORD_STANDING_BACK = "VampireLordLeftPowerAttackFeedBack";

        // Cannibal
        inline constexpr const char* CANIBAL_STANDING_FRONT = "IdleCannibalFeedStanding";
        inline constexpr const char* CANIBAL_STANDING_CROUCH = "IdleCannibalFeedCrouching";

        // Werewolf
        inline constexpr const char* WEREWOLF_STANDING_FRONT = "WerewolfPairedFeedingWithHuman";

        // Combat idle Standing
        inline constexpr const char* FRONT_KM_A = "pa_1HMKillMoveShortA";

        // Combat idle back
        inline constexpr const char* BACK_SNEAK_KM_A = "pa_1HMSneakKillBackA";
    }

    // Lightweight position and alignment structs
    struct Position {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float r = 0.0f;  // rotation in radians
    };

    struct Alignment {
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
        float scale = 1.0f;
        float rotation = 0.0f;  // rotation offset in degrees
        float sosBend = 0.0f;
    };

    // Math utilities (inline)
    inline float toDegrees(float radians) {
        return radians * 180.0f / static_cast<float>(M_PI);
    }

    inline float toRadians(float degrees) {
        return degrees * static_cast<float>(M_PI) / 180.0f;
    }

    // Normalize angle to [-PI, PI] range to prevent full circle rotations
    inline float normalizeAngle(float angle) {
        while (angle > static_cast<float>(M_PI)) angle -= 2.0f * static_cast<float>(M_PI);
        while (angle < -static_cast<float>(M_PI)) angle += 2.0f * static_cast<float>(M_PI);
        return angle;
    }

    // Calculate shortest rotation difference between two angles
    inline float angleDifference(float current, float target) {
        float diff = target - current;
        return normalizeAngle(diff);
    }

    // Core animation functions
    void playAnimation(RE::Actor* actor, const std::string& animation);
    void playAnimation(RE::Actor* actor, const std::string& animation, float playbackSpeed);
    void playIdle(RE::Actor* actor, RE::TESIdleForm* idle, RE::TESObjectREFR* target = nullptr);

    // Preprocessing for paired animations - clears stagger/attack/knockdown states
    void PrepareActorForPairedIdle(RE::Actor* actor);

    // Position and rotation functions
    void setPosition(RE::Actor* actor, float x, float y, float z, float rotation);
    void setRotation(RE::Actor* actor, float rotation);

    // Alignment function (uses 2D rotation matrix)
    void alignActor(RE::Actor* actor, Position sceneCenter, Alignment alignment, Position additionalOffset = {});

    // Stop animation and restore actor control
    void stopAnimation(RE::Actor* actor);

    // Redraw weapon/magic after animation (restores drawn state)
    void redrawWeapon(RE::Actor* actor);

    // Set actor restrained state (calls Papyrus native function)
    void setRestrained(RE::Actor* actor, bool restrained = true);

    // Continuous positioning - maintains position until stopped
    // updateFrequency: 1 = every frame, 2 = every other frame, etc. (default: 2 for ~30Hz at 60 FPS)
    void maintainActorPosition(RE::Actor* actor, float x, float y, float z, float rotation, const std::string& taskId, uint32_t updateFrequency = 2);
    void maintainPairedPosition(RE::Actor* attacker, RE::Actor* victim, bool faceOpposite, const std::string& taskId, uint32_t updateFrequency = 2);
    void stopMaintainingPosition(const std::string& taskId);

    // Native Papyrus function wrappers (via SKSE)
    void TranslateTo(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object,
        float afX, float afY, float afZ, float afAngleX, float afAngleY, float afAngleZ, float afSpeed, float afMaxRotationSpeed);
    void StopTranslation(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object);

    // Lock actor at position using TranslateTo (OStim-style positioning)
    void LockAtPosition(RE::Actor* actor, float x, float y, float z, float rotationRad, bool applyRotation = true);

    // Animation selection and state checking (moved from PairedAnimPromptSink)
    bool IsInPairedAnimation(RE::Actor* actor);
    int DetermineTargetState(RE::Actor* target, bool& outIsInCombat);
    void ApplyHeightAdjustment(RE::Actor* attacker, RE::Actor* target, float minHeightDiff, float maxHeightDiff);
    const char* SelectIdleAnimation(int targetState, RE::Actor* target,
                                    const RE::NiPointer<RE::TESObjectREFR>& furnitureRef, bool isBehind,
                                    bool& outIsPairedAnim);

    // Player feed validation (moved from PairedAnimPromptSink)
    bool IsPlayerFeedingRace();  // Check if player race supports feeding (Vampire/Werewolf/VL)
    bool CanPlayerFeed(bool targetInCombat);

    // Animation graph variable management (moved from PairedAnimPromptSink)
    void SetFeedGraphVars(RE::Actor* actor, int feedType);
    void ClearFeedGraphVars(RE::Actor* actor);

    // Actor direction and rotation utilities (moved from FeedState)
    float GetAngleBetween(RE::Actor* from, RE::Actor* to);
    bool GetClosestDirection(RE::Actor* target, RE::Actor* reference);
    bool RotateTargetToClosest(RE::Actor* target, RE::Actor* reference);
    void RotateAttackerToTarget(RE::Actor* attacker, RE::Actor* target);
    bool IsPlayerFacingTarget(RE::Actor* player, RE::Actor* target, float maxAngleDegrees = 90.0f);

    // Kill target actor (for lethal feeds)
    void KillTarget(RE::Actor* target);

    // Death time utilities
    // Returns hours since death, or -1.0f if actor is not dead or has no process
    float GetHoursSinceDeath(RE::Actor* actor);

    // Check if actor is recently dead (within maxHours)
    bool IsRecentlyDead(RE::Actor* actor, float maxHours);

    // Dead feed count tracking (in-memory, resets on game reload)
    int GetDeadFeedCount(RE::Actor* actor);
    void IncrementDeadFeedCount(RE::Actor* actor);
    bool HasExceededDeadFeedLimit(RE::Actor* actor, int maxFeeds);

    // Check if attacker's attack should kill victim (uses game's ShouldAttackKill condition)
    bool ShouldAttackKill(const RE::Actor* attacker, const RE::Actor* victim);

    // Kill move flag management (blocks Quick Loot and other activation during paired animations)
    void SetInKillMove(RE::Actor* actor, bool inKillMove);
    bool IsInKillMove(RE::Actor* actor);

    // Actor state checks
    bool IsJumping(RE::Actor* actor);
    bool IsRiding(RE::Actor* actor);
    bool IsSwimming(RE::Actor* actor);

    // =====================================================================
    // Idle Graph System - Vanilla-style idle selection based on conditions
    // =====================================================================

    // Node in the idle selection graph
    // Represents a TESIdleForm with its conditions and children
    struct IdleNode {
        RE::TESIdleForm* idle = nullptr;
        std::vector<IdleNode> children;

        // Cached condition info for debugging/logging
        std::string editorID;
        bool hasConditions = false;
    };

    // Context for evaluating idle conditions
    // Provides all actor state needed for condition checks
    struct IdleSelectionContext {
        RE::Actor* subject = nullptr;           // Actor performing the idle (usually player)
        RE::Actor* target = nullptr;            // Target of the action (victim, etc.)

        // Pre-computed state for faster condition evaluation
        float angleToTarget = 0.0f;             // Angle from subject to target (radians)
        float distanceToTarget = 0.0f;          // Distance to target
        bool isBehindTarget = false;            // Is subject behind target?
        bool isInCombat = false;                // Is subject in combat?
        bool isSneaking = false;                // Is subject sneaking?

        // Weapon info
        RE::WEAPON_TYPE weaponType = RE::WEAPON_TYPE::kHandToHandMelee;
        bool hasWeaponDrawn = false;
        bool hasTwoHandedWeapon = false;
        bool hasShield = false;
        bool hasBow = false;
        bool hasMagic = false;
    };

    // Result of idle selection with detailed info
    struct IdleSelectionResult {
        RE::TESIdleForm* selectedIdle = nullptr;
        std::string editorID;
        bool success = false;
        std::string failureReason;

        // Path through the graph (for debugging)
        std::vector<std::string> selectionPath;
    };

    // Build context from actors (computes all derived state)
    IdleSelectionContext BuildIdleContext(RE::Actor* subject, RE::Actor* target);

    // Retrieve all child idles from a parent idle form
    // Returns empty vector if no children or invalid input
    std::vector<RE::TESIdleForm*> GetChildIdles(RE::TESIdleForm* parentIdle);

    // Build a graph of idles starting from a root idle
    // Traverses parent-child hierarchy and caches condition info
    IdleNode BuildIdleGraph(RE::TESIdleForm* rootIdle, int maxDepth = 10);

    // Build graph from EditorID
    IdleNode BuildIdleGraphFromEditorID(const char* rootEditorID, int maxDepth = 10);

    // Evaluate if an idle's conditions pass for the given context
    // Uses vanilla TESCondition evaluation
    bool EvaluateIdleConditions(RE::TESIdleForm* idle, const IdleSelectionContext& context);

    // Select the best idle from a graph based on conditions
    // Traverses the tree, evaluating conditions at each node
    // Returns the deepest valid idle (most specific match)
    IdleSelectionResult SelectIdleFromGraph(const IdleNode& root, const IdleSelectionContext& context);

    // Convenience: Build graph and select in one call
    IdleSelectionResult SelectIdleFromRoot(RE::TESIdleForm* rootIdle, const IdleSelectionContext& context);
    IdleSelectionResult SelectIdleFromRootEditorID(const char* rootEditorID, const IdleSelectionContext& context);

    // =====================================================================
    // Weapon Type Utilities
    // =====================================================================

    // Get equipped weapon type from actor
    RE::WEAPON_TYPE GetEquippedWeaponType(RE::Actor* actor, bool rightHand = true);

    // Check weapon categories
    bool IsOneHandedWeapon(RE::WEAPON_TYPE type);
    bool IsTwoHandedWeapon(RE::WEAPON_TYPE type);
    bool IsRangedWeapon(RE::WEAPON_TYPE type);

    // Get attack angle (direction of swing/thrust based on weapon type)
    float GetAttackAngle(RE::Actor* attacker, RE::Actor* target);

    // =====================================================================
    // Debug / Logging Utilities
    // =====================================================================

    // Log the idle graph structure to SKSE log
    // Useful for verifying idle hierarchy at startup
    void LogIdleGraph(const IdleNode& node, int indent = 0);

    // Log all idles in the game matching a prefix (e.g. "Vampire", "pa_1HM")
    // Call during plugin load to verify idle data is accessible
    void LogIdlesByPrefix(const char* prefix);

    // Dump complete idle hierarchy starting from an EditorID
    void DumpIdleHierarchy(const char* rootEditorID, int maxDepth = 20);

    // Debug: Find and log the kill move that would match for player vs target
    // Evaluates conditions from NonMountedCombatRightPower tree and logs the result
    void DebugFindKillMove(RE::Actor* player, RE::Actor* target);
}
