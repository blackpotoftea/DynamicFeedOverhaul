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
        // Player pose locked at Play() time. Tick re-applies these every frame
        // so animation root motion can't slide the scene out from under the lock.
        RE::NiPoint3 lockedPlayerPos_{};
        float lockedPlayerYaw_ = 0.0f;
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

    // Position the TARGET around the player-centered scene. The Anub2P-style
    // paired animations bake a 180° face-off into the clips themselves (_0
    // faces skeleton +Y, _1 faces skeleton -Y), so both actors share the SAME
    // world heading and the anim files render them face-to-face.
    void PositionActorsForAnimation(RE::Actor* player, RE::Actor* target) {
        const RE::NiPoint3 center = player->GetPosition();
        const float centerAngle = player->GetAngleZ();

        auto* settings = Settings::GetSingleton();
        const float x = settings->NonCombat.TargetOffsetX;
        const float y = settings->NonCombat.TargetOffsetY;
        const float z = settings->NonCombat.TargetOffsetZ;

        AnimUtil::Position sceneCenter{center.x, center.y, center.z, centerAngle};

        // rotation=0 → target shares player's world heading. The anim clips
        // do the 180° themselves; adding it here would double-flip them and
        // they'd visually face the same direction.
        AnimUtil::Alignment targetAlignment{x, y, z, 1.0f, 0.0f, 0.0f};
        AnimUtil::alignActor(target, sceneCenter, targetAlignment);

        SKSE::log::info("Positioned target - offset: ({:.2f}, {:.2f}, {:.2f}), matching player heading", x, y, z);
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

        // Snapshot the player's pose at the start of the feed.
        lockedPlayerPos_ = player->GetPosition();
        lockedPlayerYaw_ = player->data.angle.z;

        // Disable character-vs-character collision so the two actors can
        // overlap into the embrace pose. World collision is unaffected so
        // they don't fall through the floor. kNoCharacterCollisions is the
        // CommonLib-exposed bhkCharController flag (bit 27) — symmetric, so
        // setting it on both is belt-and-suspenders.
        if (auto* cc = player->GetCharController()) {
            cc->flags.set(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
        }
        if (auto* cc = target->GetCharController()) {
            cc->flags.set(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
        }

        // Compute target's locked world pose from the snapshot + settings offset.
        const float sinR = std::sin(lockedPlayerYaw_);
        const float cosR = std::cos(lockedPlayerYaw_);
        const float offX = settings->NonCombat.TargetOffsetX;
        const float offY = settings->NonCombat.TargetOffsetY;
        const float offZ = settings->NonCombat.TargetOffsetZ;
        const float targetX = lockedPlayerPos_.x + cosR * offX + sinR * offY;
        const float targetY = lockedPlayerPos_.y - sinR * offX + cosR * offY;
        const float targetZ = lockedPlayerPos_.z + offZ;

        // Use TranslateTo via LockAtPosition — the engine-native lock that
        // suppresses anim root motion. One call per actor, no per-frame
        // fighting, no vibration. StopTranslation in OnComplete releases.
        // (Replaces the prior PositionActorsForAnimation + per-frame Tick lock.)
        AnimUtil::LockAtPosition(player, lockedPlayerPos_.x, lockedPlayerPos_.y, lockedPlayerPos_.z, lockedPlayerYaw_, true);
        AnimUtil::LockAtPosition(target, targetX, targetY, targetZ, lockedPlayerYaw_, true);

        const auto& playerAnim = settings->NonCombat.PlayerStandingFrontAnim;
        const auto& targetAnim = settings->NonCombat.TargetStandingFrontAnim;

        // Animations are Nemesis behavior-graph events, not TESIdleForms — fire
        // them directly via NotifyAnimationGraph (deferred to the game thread
        // by AnimUtil::playAnimation). No IdleForm lookup needed.
        AnimUtil::playAnimation(player, playerAnim);
        AnimUtil::playAnimation(target, targetAnim);

        SKSE::log::info("[CompositePairedAnimation] NotifyAnimationGraph: player='{}', target='{}'",
            playerAnim, targetAnim);
        SKSE::log::info("[CompositePairedAnimation] Started successfully");
        return true;
    }

    namespace {
        void RestoreCollision(RE::Actor* a) {
            if (!a) return;
            if (auto* cc = a->GetCharController()) {
                cc->flags.reset(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
            }
        }

        // Pair with AnimUtil::LockAtPosition — releases the engine's
        // TranslateTo hold so the actor can move under anim/AI control again.
        void ReleaseLock(RE::Actor* a) {
            if (!a) return;
            AnimUtil::StopTranslation(nullptr, 0, a);
        }
    }

    // Called when feed animation completes normally
    void OnComplete() {
        if (!isActive_) return;
        SKSE::log::info("[CompositePairedAnimation] OnComplete - cleaning up");
        auto* player = RE::PlayerCharacter::GetSingleton();
        ReleaseLock(player);
        RestoreCollision(player);
        if (auto target = feedTargetHandle_.get()) {
            ReleaseLock(target.get());
            RestoreCollision(target.get());
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
            ReleaseLock(player);
            RestoreCollision(player);
        }

        if (auto target = feedTargetHandle_.get()) {
             if (auto* process = target->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(target.get(), true);
             }
             ReleaseLock(target.get());
             RestoreCollision(target.get());
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

    // Lightweight per-frame health check. Position lock is held by the engine
    // via TranslateTo (set up in Play, released in OnComplete/ForceStop), so
    // there's nothing to teleport here. Tick just watches for target
    // invalidation (unload, death) and self-stops the composite state.
    void Tick() {
        if (!isActive_) return;

        auto ref = feedTargetHandle_.get();
        if (!ref) {
            isActive_ = false;
            return;
        }
        auto* target = ref->As<RE::Actor>();
        if (!target || target->IsDead()) {
            isActive_ = false;
            return;
        }
    }
}
