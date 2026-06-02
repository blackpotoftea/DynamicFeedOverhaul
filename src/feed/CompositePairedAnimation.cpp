#include "feed/CompositePairedAnimation.h"
#include "Settings.h"
#include "PCH.h"
#include "utils/AnimUtil.h"
#include <cmath>

namespace CompositePairedAnimation {

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

    // Main entry point - play two single-actor feed animations in sync
    bool Play(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) {
            SKSE::log::error("[CompositePairedAnimation] Player or target is null");
            return false;
        }

        auto* settings = Settings::GetSingleton();
        AnimUtil::setRestrained(target, true);

        SKSE::log::info("[CompositePairedAnimation] Starting composite paired animation on {} (FormID: {:X})",
            target->GetName(), target->GetFormID());

        feedTargetHandle_ = target->GetHandle();
        isActive_ = true;

        // Initial alignment around player-centered scene
        PositionActorsForAnimation(player, target);

        const auto& playerAnim = settings->NonCombat.PlayerStandingFrontAnim;
        const auto& targetAnim = settings->NonCombat.TargetStandingFrontAnim;

        auto* playerIdleForm = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(playerAnim);
        auto* targetIdleForm = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(targetAnim);

        SKSE::log::info("[CompositePairedAnimation] DEBUG: playerIdleForm lookup result: {} (looking for '{}')",
            playerIdleForm ? "FOUND" : "NULL", playerAnim);
        SKSE::log::info("[CompositePairedAnimation] DEBUG: targetIdleForm lookup result: {} (looking for '{}')",
            targetIdleForm ? "FOUND" : "NULL", targetAnim);

        if (!playerIdleForm) {
            SKSE::log::error("[CompositePairedAnimation] Failed to find player animation: '{}'", playerAnim);
        }
        if (!targetIdleForm) {
            SKSE::log::error("[CompositePairedAnimation] Failed to find target animation: '{}'", targetAnim);
        }

        AnimUtil::playIdle(player, playerIdleForm, nullptr);
        AnimUtil::playIdle(target, targetIdleForm, nullptr);

        AnimUtil::playAnimation(player, playerAnim, 1.0f);
        AnimUtil::playAnimation(target, targetAnim, 1.0f);

        SKSE::log::info("[CompositePairedAnimation] Animation triggers: player='{}' , target='{}' ",
            playerAnim, targetAnim);

        SKSE::log::info("[CompositePairedAnimation] Started successfully");
        return true;
    }

    // Called when feed animation completes normally
    void OnComplete() {
        if (!isActive_) return;
        SKSE::log::info("[CompositePairedAnimation] OnComplete - cleaning up");
        if (auto target = feedTargetHandle_.get()) {
            AnimUtil::setRestrained(target.get(), false);
        }
        feedTargetHandle_ = {};
        isActive_ = false;
    }

    // Force stop
    void ForceStop() {
        SKSE::log::info("[CompositePairedAnimation] ForceStop called");
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
             AnimUtil::setRestrained(target.get(), false);
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

    // Per-frame position lock. Called from PlayerUpdateHook. No-op on the
    // fast path when no composite feed is active.
    void Tick() {
        if (!isActive_) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto ref = feedTargetHandle_.get();
        if (!ref) {
            // Target unloaded — self-stop.
            isActive_ = false;
            return;
        }
        auto* target = ref->As<RE::Actor>();
        if (!target || target->IsDead()) {
            isActive_ = false;
            return;
        }

        auto* settings = Settings::GetSingleton();
        const float offX = settings->NonCombat.TargetOffsetX;
        const float offY = settings->NonCombat.TargetOffsetY;
        const float offZ = settings->NonCombat.TargetOffsetZ;

        const RE::NiPoint3 center = player->GetPosition();
        const float centerAngle = player->GetAngleZ();
        const float sinR = std::sin(centerAngle);
        const float cosR = std::cos(centerAngle);

        const float targetX = center.x + cosR * offX + sinR * offY;
        const float targetY = center.y - sinR * offX + cosR * offY;
        const float targetZ = center.z + offZ;

        // Face-opposite: victim looks back at player.
        const float targetRot = AnimUtil::normalizeAngle(centerAngle - static_cast<float>(M_PI));

        AnimUtil::setPosition(target, targetX, targetY, targetZ, targetRot);
    }
}
