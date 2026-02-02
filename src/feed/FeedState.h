#pragma once
#include "Settings.h"
#include <random>

// Feed type calculation for OAR graph variable conditions
// Composite value: (TargetState * 10) + VampireHungerStage
// This allows OAR to match on both target state and hunger level
//
// Target State (tens digit):
//   1x = Standing
//   2x = Sleeping
//   3x = Sitting
//   4x = Combat
//
// Vampire Hunger Stage (ones digit):
//   x1 = Stage 1 (sated)
//   x2 = Stage 2
//   x3 = Stage 3
//   x4 = Stage 4 (blood starved)
//
// Examples:
//   11 = Standing, Stage 1 (sated)
//   14 = Standing, Stage 4 (blood starved)
//   21 = Sleeping, Stage 1
//   44 = Combat, Stage 4 (most aggressive)
//
// OAR conditions can match:
//   == 14 : exactly standing + stage 4
//   >= 40 : any combat feed
//   >= 13 AND < 20 : standing + stage 3 or 4

namespace FeedState {
    // Target state base values (multiply by 10)
    constexpr int kStanding = 10;
    constexpr int kSleeping = 20;
    constexpr int kSitting = 30;
    constexpr int kCombat = 40;

    // Calculate feed type from target state and vampire hunger stage
    inline int Calculate(int targetState, int vampireStage) {
        // Clamp vampire stage to 1-4
        int stage = std::clamp(vampireStage, 1, 4);
        return targetState + stage;
    }

    // Pick random element from vector
    inline int PickRandom(const std::vector<int>& list) {
        if (list.empty()) return 0;
        if (list.size() == 1) return list[0];

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dist(0, list.size() - 1);
        return list[dist(gen)];
    }

    // Player gender for animation selection
    enum class Gender { kMale, kFemale };

    // Get player gender
    inline Gender GetPlayerGender() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto* base = player->GetActorBase();
            if (base && base->IsFemale()) {
                return Gender::kFemale;
            }
        }
        return Gender::kMale;
    }

    // Calculate angle from target to player (in radians)
    inline float GetAngleToPlayer(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return 0.0f;

        auto playerPos = player->GetPosition();
        auto targetPos = target->GetPosition();

        float dx = playerPos.x - targetPos.x;
        float dy = playerPos.y - targetPos.y;

        // atan2 gives angle from target to player
        return std::atan2(dx, dy);
    }

    // Rotate target to face toward or away from player (whichever is closer)
    // Returns true if rotated to face away (back animation), false if facing toward (front animation)
    inline bool RotateTargetToClosest(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return false;

        float angleToPlayer = GetAngleToPlayer(target);
        float currentHeading = target->GetAngleZ();

        // Normalize angle difference to -PI to PI
        float diffToFront = angleToPlayer - currentHeading;
        while (diffToFront > 3.14159f) diffToFront -= 6.28318f;
        while (diffToFront < -3.14159f) diffToFront += 6.28318f;

        // Back angle is opposite (180 degrees / PI radians)
        float backAngle = angleToPlayer + 3.14159f;
        float diffToBack = backAngle - currentHeading;
        while (diffToBack > 3.14159f) diffToBack -= 6.28318f;
        while (diffToBack < -3.14159f) diffToBack += 6.28318f;

        bool useBack = std::fabs(diffToBack) < std::fabs(diffToFront);
        float newAngle = useBack ? backAngle : angleToPlayer;

        // Normalize new angle to 0 to 2PI
        while (newAngle < 0) newAngle += 6.28318f;
        while (newAngle >= 6.28318f) newAngle -= 6.28318f;

        SKSE::log::info("Rotate target: current={:.2f}, toFront={:.2f} (diff={:.2f}), toBack={:.2f} (diff={:.2f}) -> {} (new={:.2f})",
            currentHeading, angleToPlayer, diffToFront, backAngle, diffToBack,
            useBack ? "BACK" : "FRONT", newAngle);

        // SetAngle takes NiPoint3 with x, y, z angles - we only change z (heading)
        RE::NiPoint3 angles = target->data.angle;
        angles.z = newAngle;
        target->SetAngle(angles);
        return useBack;
    }

    // TODO  refactor SelectAnimation set graph varible not actuaply picking animation
    // Select FeedType based on combat state, hunger level, position, and player gender
    // Priority: position (front/back) > gender (female > unisex fallback)
    // Returns 0 if no animations configured (falls back to default calculation)
    inline int SelectAnimation(bool isInCombat, int vampireStage, RE::Actor* target) {
        auto* settings = Settings::GetSingleton();

        if (!settings->Animation.EnableRandomSelection) {
            return 0;  // Use default calculation
        }

        bool isHungry = vampireStage >= settings->Animation.HungryThreshold;
        bool isBehind;

        if (settings->General.SequentialPlay) {
            // Debug mode: sequential animation playthrough
            // 1. Determine front/back based on closest rotation
            // 2. Collect all unisex animations for that direction (ignore hunger/gender)
            // 3. Play sequentially, cycling through all before repeating

            static std::vector<int> sequentialList;
            static size_t sequentialIndex = 0;
            static bool lastWasBack = false;

            // Determine position and rotate target
            isBehind = RotateTargetToClosest(target);

            // If direction changed or list is empty, rebuild the list
            if (sequentialList.empty() || isBehind != lastWasBack) {
                sequentialList.clear();
                sequentialIndex = 0;
                lastWasBack = isBehind;

                auto& a = settings->Animation;
                if (isBehind) {
                    sequentialList.insert(sequentialList.end(), a.SatedBackUnisex.begin(), a.SatedBackUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.HungryBackUnisex.begin(), a.HungryBackUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.CombatSatedBackUnisex.begin(), a.CombatSatedBackUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.CombatHungryBackUnisex.begin(), a.CombatHungryBackUnisex.end());
                } else {
                    sequentialList.insert(sequentialList.end(), a.SatedFrontUnisex.begin(), a.SatedFrontUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.HungryFrontUnisex.begin(), a.HungryFrontUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.CombatSatedFrontUnisex.begin(), a.CombatSatedFrontUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.CombatHungryFrontUnisex.begin(), a.CombatHungryFrontUnisex.end());
                }

                SKSE::log::info("SequentialPlay: rebuilt {} list with {} animations",
                    isBehind ? "back" : "front", sequentialList.size());
            }

            if (!sequentialList.empty()) {
                // Wrap around if we've gone through all
                if (sequentialIndex >= sequentialList.size()) {
                    sequentialIndex = 0;
                    SKSE::log::info("SequentialPlay: cycling back to start");
                }

                int selected = sequentialList[sequentialIndex];
                SKSE::log::info("SequentialPlay: {} [{}/{}] FeedType={}",
                    isBehind ? "back" : "front", sequentialIndex + 1, sequentialList.size(), selected);
                sequentialIndex++;
                return selected;
            }

            SKSE::log::warn("SequentialPlay: no {} animations configured", isBehind ? "back" : "front");
            return 0;
        }

        // Normal mode: rotate target to closest direction and select based on all criteria
        isBehind = RotateTargetToClosest(target);
        Gender gender = GetPlayerGender();

        const std::vector<int>* femaleList = nullptr;
        const std::vector<int>* unisexList = nullptr;

        if (isInCombat) {
            if (isHungry) {
                if (isBehind) {
                    femaleList = &settings->Animation.CombatHungryBackFemale;
                    unisexList = &settings->Animation.CombatHungryBackUnisex;
                } else {
                    femaleList = &settings->Animation.CombatHungryFrontFemale;
                    unisexList = &settings->Animation.CombatHungryFrontUnisex;
                }
            } else {
                if (isBehind) {
                    femaleList = &settings->Animation.CombatSatedBackFemale;
                    unisexList = &settings->Animation.CombatSatedBackUnisex;
                } else {
                    femaleList = &settings->Animation.CombatSatedFrontFemale;
                    unisexList = &settings->Animation.CombatSatedFrontUnisex;
                }
            }
        } else {
            if (isHungry) {
                if (isBehind) {
                    femaleList = &settings->Animation.HungryBackFemale;
                    unisexList = &settings->Animation.HungryBackUnisex;
                } else {
                    femaleList = &settings->Animation.HungryFrontFemale;
                    unisexList = &settings->Animation.HungryFrontUnisex;
                }
            } else {
                if (isBehind) {
                    femaleList = &settings->Animation.SatedBackFemale;
                    unisexList = &settings->Animation.SatedBackUnisex;
                } else {
                    femaleList = &settings->Animation.SatedFrontFemale;
                    unisexList = &settings->Animation.SatedFrontUnisex;
                }
            }
        }

        const char* posStr = isBehind ? "back" : "front";

        // For female players, try female-specific list first
        if (gender == Gender::kFemale && femaleList && !femaleList->empty()) {
            int selected = PickRandom(*femaleList);
            SKSE::log::info("Animation selection: combat={}, hungry={}, pos={}, gender=female, FeedType={} (female-specific)",
                isInCombat, isHungry, posStr, selected);
            return selected;
        }

        // Use unisex list (for male players, or female fallback)
        if (unisexList && !unisexList->empty()) {
            int selected = PickRandom(*unisexList);
            SKSE::log::info("Animation selection: combat={}, hungry={}, pos={}, gender={}, FeedType={} (unisex)",
                isInCombat, isHungry, posStr, (gender == Gender::kFemale ? "female" : "male"), selected);
            return selected;
        }

        return 0;  // No animations configured, use default
    }
}
