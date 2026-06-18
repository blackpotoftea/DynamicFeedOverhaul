#include "feed/CompositePairedAnimation.h"
#include "Settings.h"
#include "PCH.h"
#include "utils/AnimUtil.h"
#include "feed/CombatBark.h"
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

        // Phase 2 (deferred) state. After Play() queues the head-tracking
        // disable, we wait a few frames for the graph variable changes to
        // settle in the behavior graph BEFORE firing the position lock and
        // anim trigger. Otherwise the new anim's transition blend begins
        // mid-evaluation with stale head-tracking IK still pulling on the
        // spine — that's the source of the visible first-frame rotation.
        // Tick() counts down and fires the deferred phase exactly once.
        int settleFramesRemaining_ = 0;
        RE::NiPoint3 pendingTargetPos_{};
        std::string pendingPlayerAnim_;
        std::string pendingTargetAnim_;
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

        // Pacify the target unconditionally for the composite path. NPC
        // head-tracking / look-at-player AI was slewing the target's heading
        // each frame and causing a visible first-time rotation spin before
        // the lock landed. Pacify halts that AI control; UndoPacifyActor
        // is called by PairedAnimation::ExitFeedState during cleanup.
        // AnimUtil::PacifyActor(target);

        // Disable head-tracking + foot IK graph variables on both actors.
        // Pacify stops AI packages but the behavior graph's internal
        // head-tracking IK still slews the actor's effective rotation each
        // frame — that's the real source of the visible first-time spin.
        // Restored via UnlockActorForPairedAnim in OnComplete / ForceStop.
        // (OStimNG's GameActor::lock uses the same four graph vars.)
        AnimUtil::LockActorForPairedAnim(player);
        AnimUtil::LockActorForPairedAnim(target);

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
        pendingTargetPos_ = RE::NiPoint3{
            lockedPlayerPos_.x + cosR * offX + sinR * offY,
            lockedPlayerPos_.y - sinR * offX + cosR * offY,
            lockedPlayerPos_.z + offZ
        };

        // Re-assert PLAYER heading directly (bypasses SetAngle interpolation).
        // Target heading is NOT touched here — RotateTargetToClosest already
        // set the target to face-opposite (= facing the player), which is
        // what the asset-fixed clip expects. A direct face-same write here
        // would undo that and the actors would face the same direction.
        player->data.angle.z = lockedPlayerYaw_;

        // Stash anim names + arm the deferred phase. Tick() fires the
        // position lock and NotifyAnimationGraph after settleFramesRemaining_
        // hits zero — gives the head-tracking graph vars (queued above by
        // LockActorForPairedAnim) at least 2-3 game-thread updates to settle
        // before the new anim's transition blend starts. Without this delay,
        // the blend begins with stale head-tracking IK and visibly spins.
        pendingPlayerAnim_ = settings->NonCombat.PlayerStandingFrontAnim;
        pendingTargetAnim_ = settings->NonCombat.TargetStandingFrontAnim;
        settleFramesRemaining_ = 3;

        SKSE::log::info("[CompositePairedAnimation] Play stage 1 queued (graph vars + pacify); stage 2 deferred {} frames", settleFramesRemaining_);
        return true;
    }

    // Deferred stage 2: fires from Tick() once head-tracking graph vars have
    // had a few frames to settle. Locks both actors at their target world
    // poses via TranslateTo and triggers the Nemesis graph events.
    namespace {
        void FireDeferredPlay() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto targetRef = feedTargetHandle_.get();
            if (!player || !targetRef) {
                SKSE::log::warn("[CompositePairedAnimation] Deferred play aborted (actor missing)");
                isActive_ = false;
                return;
            }
            auto* target = targetRef->As<RE::Actor>();
            if (!target) {
                isActive_ = false;
                return;
            }

            // Re-assert PLAYER heading. Target heading is left alone — it was
            // set by RotateTargetToClosest (face-opposite of player), which
            // is what the fixed-asset clip expects. Don't write face-same here.
            player->data.angle.z = lockedPlayerYaw_;
            const float targetYaw = target->data.angle.z;

            AnimUtil::LockAtPosition(player, lockedPlayerPos_.x, lockedPlayerPos_.y, lockedPlayerPos_.z, lockedPlayerYaw_, false);
            AnimUtil::LockAtPosition(target, pendingTargetPos_.x, pendingTargetPos_.y, pendingTargetPos_.z, targetYaw, false);

            AnimUtil::playAnimation(player, pendingPlayerAnim_);
            AnimUtil::playAnimation(target, pendingTargetAnim_);

            // TODO(bark): emit a victim pain bark here once CombatBark works.
            // CombatBark::Play(target, CombatBark::Type::Hit) is wired and
            // dispatches, but ObjectReference.Say selects no info for a
            // pacified target (combat-topic conditions fail) so it's silent.
            // See CombatBark.h for the investigation and the options.

            SKSE::log::info("[CompositePairedAnimation] NotifyAnimationGraph: player='{}', target='{}'",
                pendingPlayerAnim_, pendingTargetAnim_);
            SKSE::log::info("[CompositePairedAnimation] Started successfully (deferred)");
        }
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
        AnimUtil::UnlockActorForPairedAnim(player);
        if (auto target = feedTargetHandle_.get()) {
            ReleaseLock(target.get());
            RestoreCollision(target.get());
            AnimUtil::UnlockActorForPairedAnim(target.get());
            AnimUtil::setRestrained(target.get(), false);
            // Re-evaluate AI packages so the NPC un-parks and resumes its
            // routine after the graph reset (mirrors OStimNG updateAI()).
            AnimUtil::RefreshActorAI(target.get());
        }
        feedTargetHandle_ = {};
        isActive_ = false;
        settleFramesRemaining_ = 0;
    }

    // Force stop
    void ForceStop() {
        SKSE::log::info("[CompositePairedAnimation] ForceStop called");
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            // StopCurrentIdle only tears down a PlayIdle/SetupSpecialIdle slot.
            // Composite clips are started via NotifyAnimationGraph (raw behavior
            // events), so no special idle is registered and this is a no-op.
            // The graph state is exited by UnlockActorForPairedAnim's
            // IdleForceDefaultState event instead. (Both OStimNG and GTS omit it.)
            // if (auto* process = player->GetActorRuntimeData().currentProcess) {
            //     process->StopCurrentIdle(player, true);
            // }
            ReleaseLock(player);
            RestoreCollision(player);
            AnimUtil::UnlockActorForPairedAnim(player);
        }

        if (auto target = feedTargetHandle_.get()) {
             // No-op for composite clips — see player note above.
             // if (auto* process = target->GetActorRuntimeData().currentProcess) {
             //     process->StopCurrentIdle(target.get(), true);
             // }
             ReleaseLock(target.get());
             RestoreCollision(target.get());
             AnimUtil::UnlockActorForPairedAnim(target.get());
             AnimUtil::setRestrained(target.get(), false);
             // Re-evaluate AI packages so the NPC un-parks and resumes its
             // routine after the graph reset (mirrors OStimNG updateAI()).
             AnimUtil::RefreshActorAI(target.get());
        }
        feedTargetHandle_ = {};
        isActive_ = false;
        settleFramesRemaining_ = 0;
    }

    bool IsActive() { return isActive_; }
    RE::NiPointer<RE::Actor> GetFeedTarget() {
        auto ref = feedTargetHandle_.get();
        if (!ref) return nullptr;
        return RE::NiPointer<RE::Actor>(ref->As<RE::Actor>());
    }

    // Lightweight per-frame driver. Position lock is held by the engine via
    // TranslateTo (set up in Play stage 2, released in OnComplete/ForceStop).
    // This also fires the deferred Play stage 2 once head-tracking graph vars
    // have settled, and watches for target invalidation (unload, death).
    void Tick() {
        if (!isActive_) return;

        auto ref = feedTargetHandle_.get();
        if (!ref) {
            isActive_ = false;
            settleFramesRemaining_ = 0;
            return;
        }
        auto* target = ref->As<RE::Actor>();
        if (!target || target->IsDead()) {
            isActive_ = false;
            settleFramesRemaining_ = 0;
            return;
        }

        // Deferred Play stage 2 — fires exactly once when the countdown
        // reaches zero. Decrement-then-check so 3 means "fire on the 3rd Tick
        // after Play()", giving the graph vars a few evaluations to settle.
        if (settleFramesRemaining_ > 0) {
            --settleFramesRemaining_;
            if (settleFramesRemaining_ == 0) {
                FireDeferredPlay();
            }
        }
    }
}
