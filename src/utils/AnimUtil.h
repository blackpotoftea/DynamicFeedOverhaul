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

    // Position and rotation functions
    void setPosition(RE::Actor* actor, float x, float y, float z, float rotation);
    void setRotation(RE::Actor* actor, float rotation);

    // Alignment function (uses 2D rotation matrix)
    void alignActor(RE::Actor* actor, Position sceneCenter, Alignment alignment, Position additionalOffset = {});

    // Stop animation and restore actor control
    void stopAnimation(RE::Actor* actor);

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
    void ApplyHeightAdjustment(RE::PlayerCharacter* player, RE::Actor* target, float minHeightDiff, float maxHeightDiff);
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
    float GetAngleToPlayer(RE::Actor* target);
    bool GetClosestDirection(RE::Actor* target);
    bool RotateTargetToClosest(RE::Actor* target);

    // Kill target actor (for lethal feeds)
    void KillTarget(RE::Actor* target);
}
