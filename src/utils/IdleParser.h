#pragma once

#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <string>
#include <vector>

namespace IdleParser {
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
    IdleSelectionResult SelectIdleFromGraph(const IdleNode& root, const IdleSelectionContext& context, bool verbose = false);

    // Convenience: Build graph and select in one call
    IdleSelectionResult SelectIdleFromRoot(RE::TESIdleForm* rootIdle, const IdleSelectionContext& context);
    IdleSelectionResult SelectIdleFromRootEditorID(const char* rootEditorID, const IdleSelectionContext& context);

    // =====================================================================
    // Condition Bypass Utilities
    // =====================================================================
    // Temporarily clear conditions on an idle and all its parent idles
    // This allows PlayIdle to succeed without condition checks
    // MUST call RestoreIdleConditions after playing to restore original state
    // NOTE: These must be called on game thread (inside SKSE task)

    // Clear conditions on idle and all parents (saves state internally)
    void ClearIdleConditions(RE::TESIdleForm* idle);

    // Restore previously cleared conditions
    void RestoreIdleConditions();

    // Play idle with conditions bypassed (handles clear/restore automatically)
    // This queues the play to game thread with proper condition management
    void PlayIdleBypassConditions(RE::Actor* actor, RE::TESIdleForm* idle, RE::TESObjectREFR* target = nullptr);

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

    // Helper to get condition function name from function ID
    const char* GetConditionFunctionName(int funcID);

    // Log the idle graph structure to SKSE log
    // Useful for verifying idle hierarchy at startup
    void LogIdleGraph(const IdleNode& node, int indent = 0);

    // Log all idles in the game matching a prefix (e.g. "Vampire", "pa_1HM")
    // Call during plugin load to verify idle data is accessible
    void LogIdlesByPrefix(const char* prefix);

    // Dump complete idle hierarchy starting from an EditorID
    void DumpIdleHierarchy(const char* rootEditorID, int maxDepth = 20);

    // Log all conditions of a specific idle by EditorID
    void LogIdleConditions(const char* idleEditorID);

    // Debug: Find and log the kill move that would match for player vs target
    // Evaluates conditions from NonMountedCombatRightPower tree and logs the result
    void DebugFindKillMove(RE::Actor* player, RE::Actor* target);
}
