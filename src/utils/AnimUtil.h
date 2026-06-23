#pragma once

#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <cmath>
#include <functional>

#define _USE_MATH_DEFINES
#include <math.h>

namespace AnimUtil {
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

    // Callback type for playIdle result - called on game thread after PlayIdle attempt
    // Parameters: success (true if animation started), target actor (may be null for non-paired)
    using PlayIdleCallback = std::function<void(bool success, RE::Actor* target)>;

    // Core animation functions
    void playAnimation(RE::Actor* actor, const std::string& animation);
    void playAnimation(RE::Actor* actor, const std::string& animation, float playbackSpeed);
    void playIdle(RE::Actor* actor, RE::TESIdleForm* idle, RE::TESObjectREFR* callbackTarget = nullptr,
                  PlayIdleCallback callback = nullptr, bool isPaired = true);

    // Frame-driven retry for paired PlayIdle attempts. Must be called from the
    // game thread (PlayerUpdateHook). If the "KillMoveStart" animation-graph
    // event has not fired within ~50ms of a paired PlayIdle, retry the call up
    // to 5 times before signaling failure via the original callback.
    void TickPlayIdleRetry();

    // Play the currently configured failure sound once, immediately. Used by
    // the in-game settings UI to preview the sound without triggering a feed.
    void PlayFailureSoundTest();

    // Clears engine-level animation-graph blockers (current idle, stagger/attack
    // notifications, knock-down recovery) so a subsequent PlayIdle can succeed.
    // Idle-playback plumbing - applies no feed-specific policy.
    void FlushAnimationGraph(RE::Actor* actor);

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

    // Pacify actor - stops combat and prevents re-aggro during feed animation
    // Uses native ProcessLists functions for immediate effect (no VM delay)
    void PacifyActor(RE::Actor* actor);

    // Undo pacify - releases actor from pacified state
    void UndoPacifyActor(RE::Actor* actor);

    // Check if actor is currently pacified
    bool IsActorPacified(RE::Actor* actor);

    // Lock an actor for a paired-animation scene. Disables head-tracking and
    // foot-IK behavior-graph variables so the NPC's vanilla look-at-player
    // logic can't slew the effective rotation on the first frame of the new
    // anim — that slew is the cause of the visible "first-time spin" when
    // entering composite paired animations. Mirrors OStimNG GameActor::lock().
    // Pairs with UnlockActorForPairedAnim; must be called on every cleanup
    // path or NPCs will lose head-tracking permanently.
    void LockActorForPairedAnim(RE::Actor* actor);

    // Restore head-tracking + foot IK graph variables to their default-on
    // state. Symmetric to LockActorForPairedAnim.
    void UnlockActorForPairedAnim(RE::Actor* actor);

    // Kick the actor's AI to re-evaluate its packages so it resumes normal
    // behavior after a paired scene. No-op on the player (the player has no
    // package to re-pick). Mirrors OStimNG GameActor::updateAI(), which calls
    // EvaluatePackage on scene teardown — without it the NPC's AI stays parked
    // and the actor can stand frozen even after the graph reset.
    void RefreshActorAI(RE::Actor* actor);

    // Continuous positioning - maintains position until stopped
    // updateFrequency: 1 = every frame, 2 = every other frame, etc. (default: 2 for ~30Hz at 60 FPS)
    void maintainActorPosition(RE::Actor* actor, float x, float y, float z, float rotation, const std::string& taskId, uint32_t updateFrequency = 2);

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

    // Ground height detection via raycast
    // Returns detailed hit info from multi-ray fan pattern for accurate ground detection
    struct GroundHit {
        bool hit = false;                              // True if ground was detected
        float groundZ = 0.f;                           // Z coordinate of ground surface
        RE::NiAVObject* surface = nullptr;             // NiAVObject that was hit
        RE::TESObjectREFR* ref = nullptr;              // Reference if surface has userData
        RE::NiPoint3 hitNormal = {0.f, 0.f, 1.f};      // Surface normal (for slope detection)
    };

    GroundHit GetGroundHeight(RE::Actor* actor);

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
    // Force the target to face toward (faceAway=false) or away from the reference.
    void RotateTargetToReference(RE::Actor* target, RE::Actor* reference, bool faceAway);
    void RotateAttackerToTarget(RE::Actor* attacker, RE::Actor* target);
    bool IsPlayerFacingTarget(RE::Actor* player, RE::Actor* target, float maxAngleDegrees = 90.0f);

    // Kill target actor (for lethal feeds)
    void KillTarget(RE::Actor* target);

    // Drain a percentage of current health from the actor.
    // floorAtOne=true caps the drain at 1 HP so it can never kill (paired
    // animation delivers the death — preserves kill-credit / OAR / vampire
    // integration). floorAtOne=false allows the drain to reduce HP to 0.
    // Deferred to the main thread internally.
    void DrainHealthChunk(RE::Actor* actor, float percent, bool floorAtOne = true);

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
}
