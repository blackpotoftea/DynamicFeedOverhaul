#include "feed/TwoSingleFeed.h"
#include "Settings.h"
#include "PCH.h"
#include "utils/AnimUtil.h"
#include <cmath>

namespace TwoSingleFeed {

    namespace {
        // Internal state
        RE::ActorHandle feedTargetHandle_;
        bool isActive_ = false;

    }
    
    void PositionActorsForAnimationTranslate(RE::Actor* player, RE::Actor* target) {
        // 1. Get Player Data
        RE::NiPoint3 center = player->GetPosition();
        float centerAngle = player->GetAngleZ();
        float sinR = std::sin(centerAngle);
        float cosR = std::cos(centerAngle);

        // 2. Get Settings
        auto* settings = Settings::GetSingleton();
        float x = settings->NonCombat.TargetOffsetX;
        float y = settings->NonCombat.TargetOffsetY;
        float z = settings->NonCombat.TargetOffsetZ;

        // 3. Calculate Target Position (Your Matrix was correct!)
        float targetX = center.x + cosR * x + sinR * y;
        float targetY = center.y - sinR * x + cosR * y;
        float targetZ = center.z + z;

        // 4. Calculate Rotation (Degrees)
        // If you want them to face the player, use centerAngle + PI. 
        // If you want them to face same way as player, use centerAngle.
        // TranslateTo needs DEGREES.
        float targetAngleDeg = (centerAngle) * (180.0f / 3.141592653589793f);

        SKSE::log::info("Locking NPC to: {:.2f}, {:.2f}, {:.2f} Angle: {:.2f}", targetX, targetY, targetZ, targetAngleDeg);

        // 5. THE FIX: TranslateTo
        // Speed 100000 = Instant.
        AnimUtil::TranslateTo(nullptr, 0, target, targetX, targetY, targetZ, 0.0f, 0.0f, targetAngleDeg, 100000.0f, 0.0f);
        AnimUtil::TranslateTo(nullptr, 0, player, center.x, center.y, center.z, 0.0f, 0.0f, centerAngle, 100000.0f, 0.0f);
    }

    // Position BOTH actors using OStim's center-point + rotation transform pattern
    void PositionActorsForAnimation(RE::Actor* player, RE::Actor* target) {
        // Get Player Data
        RE::NiPoint3 center = player->GetPosition();
        float centerAngle = player->GetAngleZ();

        // Get Settings
        auto* settings = Settings::GetSingleton();
        float x = settings->NonCombat.TargetOffsetX;
        float y = settings->NonCombat.TargetOffsetY;
        float z = settings->NonCombat.TargetOffsetZ;

        // Set up scene center from player position
        AnimUtil::Position sceneCenter{center.x, center.y, center.z, centerAngle};

        // Position player at scene center (no offset)
        AnimUtil::Alignment playerAlignment{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        AnimUtil::alignActor(player, sceneCenter, playerAlignment);
        
        // Position target with offset from settings
        AnimUtil::Alignment targetAlignment{x, y, z, 1.0f, 0.0f, 0.0f};
        AnimUtil::alignActor(target, sceneCenter, targetAlignment);

        SKSE::log::info("Positioned actors using AnimUtil - Target offset: ({:.2f}, {:.2f}, {:.2f})", x, y, z);
    }

    // Main entry point - play two single-actor feed animations
    bool PlayTwoSingleFeed(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) {
            SKSE::log::error("[TwoSingleFeed] Player or target is null");
            return false;
        }

        auto* settings = Settings::GetSingleton();
        AnimUtil::setRestrained(target, true);

        SKSE::log::info("[TwoSingleFeed] Starting two-single feed on {} (FormID: {:X})",
            target->GetName(), target->GetFormID());

        feedTargetHandle_ = target->GetHandle();
        isActive_ = true;

        // Position and lock BOTH actors (like OStim)
        // PositionActorsForAnimation(player, target);



        const auto& playerAnim = settings->NonCombat.PlayerStandingFrontAnim;
        const auto& targetAnim = settings->NonCombat.TargetStandingFrontAnim;

        auto* playerIdleForm = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(playerAnim);
        auto* targetIdleForm = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(targetAnim);

        SKSE::log::info("[TwoSingleFeed] DEBUG: playerIdleForm lookup result: {} (looking for '{}')",
            playerIdleForm ? "FOUND" : "NULL", playerAnim);
        SKSE::log::info("[TwoSingleFeed] DEBUG: targetIdleForm lookup result: {} (looking for '{}')",
            targetIdleForm ? "FOUND" : "NULL", targetAnim);

        if (!playerIdleForm) {
            SKSE::log::error("[TwoSingleFeed] Failed to find player animation: '{}'", playerAnim);
        }
        if (!targetIdleForm) {
            SKSE::log::error("[TwoSingleFeed] Failed to find target animation: '{}'", targetAnim);
        }

        AnimUtil::playIdle(player, playerIdleForm, nullptr);
        AnimUtil::playIdle(target, targetIdleForm, nullptr);

        AnimUtil::playAnimation(player, playerAnim, 1.0f);
        AnimUtil::playAnimation(target, targetAnim, 1.0f);

        SKSE::log::info("[TwoSingleFeed] Animation triggers: player='{}' , target='{}' ",
            playerAnim, targetAnim);

        SKSE::log::info("[TwoSingleFeed] Started successfully");
        return true;
    }

    // Called when feed animation completes normally
    void OnComplete() {
        if (!isActive_) return;
        SKSE::log::info("[TwoSingleFeed] OnComplete - cleaning up");
        // StopContinuousLock();
        feedTargetHandle_ = {}; // Reset handle
        isActive_ = false;
        
    }

    // Force stop
    void ForceStop() {
        SKSE::log::info("[TwoSingleFeed] ForceStop called");
        // StopContinuousLock();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            if (auto* process = player->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(player, true);
            }
        }
        
        if (auto target = feedTargetHandle_.get()) {
             if (auto* process = target->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(target.get(), true);
             }
        }
        feedTargetHandle_ = {};
        isActive_ = false;
    }

    bool IsActive() { return isActive_; }
    RE::NiPointer<RE::Actor> GetFeedTarget() {
        auto ref = feedTargetHandle_.get();
        if (!ref) return nullptr;
        return RE::NiPointer<RE::Actor>(ref->As<RE::Actor>());
    }
}
