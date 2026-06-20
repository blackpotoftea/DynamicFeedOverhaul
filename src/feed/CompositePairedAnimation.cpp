#include "feed/CompositePairedAnimation.h"
#include "feed/FeedAnimState.h"
#include "Settings.h"
#include "PCH.h"
#include "utils/AnimUtil.h"
#include "feed/CombatBark.h"
#include "feed/FeedHealthBarOverlay.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace CompositePairedAnimation {

    namespace {
        // Lifecycle stages. Active == not Idle/Done (see IsActive()).
        enum class Stage { Idle, Settle, Intro, Loop, Exit, Killing, Done };

        RE::ActorHandle feedTargetHandle_;
        Stage stage_ = Stage::Idle;
        float stageTimer_ = 0.0f;   // seconds elapsed in the current stage
        float gulpTimer_ = 0.0f;    // counts down to the next drain "gulp" during Loop

        // Uniform random in [lo, hi] (handles lo > hi / lo == hi).
        float RandRange(float lo, float hi) {
            if (hi < lo) std::swap(lo, hi);
            if (hi <= lo) return lo;
            thread_local std::mt19937 gen{ std::random_device{}() };
            std::uniform_real_distribution<float> dist(lo, hi);
            return dist(gen);
        }

        // Settle phase: wait a few frames after Play() so the head-tracking /
        // foot-IK graph-var changes settle before the Intro blend starts,
        // otherwise stale IK visibly spins the actor on the first frame.
        int settleFramesRemaining_ = 0;

        // Selected clip set for this feed.
        Feed::CompositePack pack_{};

        // Player pose locked at Play() time. Re-applied on every stage change
        // so animation root motion can't slide the scene out from under the lock.
        RE::NiPoint3 lockedPlayerPos_{};
        float lockedPlayerYaw_ = 0.0f;
        RE::NiPoint3 pendingTargetPos_{};
    }

    bool IsActive() { return stage_ != Stage::Idle && stage_ != Stage::Done; }

    RE::NiPointer<RE::Actor> GetFeedTarget() {
        auto ref = feedTargetHandle_.get();
        if (!ref) return nullptr;
        return RE::NiPointer<RE::Actor>(ref->As<RE::Actor>());
    }

    namespace {
        void RestoreCollision(RE::Actor* a) {
            if (!a) return;
            if (auto* cc = a->GetCharController()) {
                cc->flags.reset(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
            }
        }

        // Pair with AnimUtil::LockAtPosition — releases the engine's TranslateTo
        // hold so the actor can move under anim/AI control again.
        void ReleaseLock(RE::Actor* a) {
            if (!a) return;
            AnimUtil::StopTranslation(nullptr, 0, a);
        }

        // Send one clip's animation event on the main thread and log the actual
        // NotifyAnimationGraph result. `who`/`stageName` are string literals so
        // they're safe to capture by value into the deferred task.
        void NotifyClipLogged(RE::Actor* actor, const std::string& anim, const char* stageName, const char* who) {
            if (!actor || anim.empty()) {
                SKSE::log::info("[CompositePairedAnimation] [{}] {} clip: <none> (skipped)", stageName, who);
                return;
            }
            const std::string actorName = actor->GetName();
            auto handle = actor->CreateRefHandle();
            SKSE::GetTaskInterface()->AddTask([handle, anim, stageName, who, actorName] {
                auto ref = handle.get();
                if (auto* a = ref.get()) {
                    const bool ok = a->NotifyAnimationGraph(anim);
                    SKSE::log::info("[CompositePairedAnimation] [{}] {} ({}) NotifyAnimationGraph('{}') => {}",
                        stageName, who, actorName, anim, ok ? "OK" : "REJECTED");
                } else {
                    SKSE::log::warn("[CompositePairedAnimation] [{}] {} clip '{}' dropped (actor gone)",
                        stageName, who, anim);
                }
            });
        }

        // Re-lock both actors at the scene poses and notify the clip pair for
        // the given stage. Called on each stage entry; re-locking keeps the
        // pair pinned across the transition blend. Empty clip names are skipped.
        void FireStageClips(const Feed::StageClips& clips, const char* stageName) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto targetRef = feedTargetHandle_.get();
            if (!player || !targetRef) return;
            auto* target = targetRef->As<RE::Actor>();
            if (!target) return;

            // Re-assert PLAYER heading (bypasses SetAngle interpolation). Target
            // heading is left alone — RotateTargetToClosest already set it to
            // face the player, which is what the fixed-asset clips expect.
            player->data.angle.z = lockedPlayerYaw_;
            const float targetYaw = target->data.angle.z;

            AnimUtil::LockAtPosition(player, lockedPlayerPos_.x, lockedPlayerPos_.y, lockedPlayerPos_.z, lockedPlayerYaw_, false);
            AnimUtil::LockAtPosition(target, pendingTargetPos_.x, pendingTargetPos_.y, pendingTargetPos_.z, targetYaw, false);

            SKSE::log::info("[CompositePairedAnimation] >>> {} stage: firing player='{}', target='{}'",
                stageName, clips.player.empty() ? "<none>" : clips.player,
                clips.target.empty() ? "<none>" : clips.target);

            NotifyClipLogged(player, clips.player, stageName, "player");
            NotifyClipLogged(target, clips.target, stageName, "target");

            // TODO(bark): emit a victim pain bark here once CombatBark works.
            // CombatBark::Play(target, CombatBark::Type::Hit) is wired and
            // dispatches, but ObjectReference.Say selects no info for a
            // pacified target (combat-topic conditions fail) so it's silent.
            // See CombatBark.h for the investigation and the options.
        }

        // Idempotent per-actor cleanup. Sets stage_ = Idle FIRST so any
        // re-entrant call (e.g. via MarkFeedEnded -> OnComplete) is a no-op.
        void DoTeardown() {
            if (stage_ == Stage::Idle) return;
            stage_ = Stage::Idle;
            stageTimer_ = 0.0f;
            settleFramesRemaining_ = 0;

            SKSE::log::info("[CompositePairedAnimation] Teardown");
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                ReleaseLock(player);
                RestoreCollision(player);
                AnimUtil::UnlockActorForPairedAnim(player);
            }
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
        }

        // Full completion: teardown + notify the shared feed state. MarkFeedEnded
        // calls OnComplete() -> DoTeardown() again, but DoTeardown already set
        // stage_ = Idle so that nested call is a no-op (single execution).
        void Finish() {
            DoTeardown();
            FeedAnimState::MarkFeedEnded();
        }
    }

    bool Play(RE::Actor* target, const Feed::CompositePack& pack) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) {
            SKSE::log::error("[CompositePairedAnimation] Player or target is null");
            return false;
        }

        auto* settings = Settings::GetSingleton();
        AnimUtil::setRestrained(target, true);

        SKSE::log::info("[CompositePairedAnimation] Starting staged feed on {} (FormID: {:X}), pack '{}'",
            target->GetName(), target->GetFormID(), pack.name);

        feedTargetHandle_ = target->GetHandle();
        pack_ = pack;
        stage_ = Stage::Settle;
        stageTimer_ = 0.0f;

        // Show the victim's health bar for the whole feed (drains live as HP
        // drops). Hidden centrally in FeedAnimState::MarkFeedEnded().
        FeedHealthBarOverlay::GetSingleton()->Show(target);

        // Snapshot the player's pose at the start of the feed.
        lockedPlayerPos_ = player->GetPosition();
        lockedPlayerYaw_ = player->data.angle.z;

        // Disable head-tracking + foot IK graph variables on both actors. The
        // behavior graph's head-tracking IK slews the actor's effective rotation
        // each frame — the source of the visible first-frame spin. Restored via
        // UnlockActorForPairedAnim in DoTeardown. (OStimNG's GameActor::lock uses
        // the same four graph vars.)
        AnimUtil::LockActorForPairedAnim(player);
        AnimUtil::LockActorForPairedAnim(target);

        // Disable character-vs-character collision so the two actors can overlap
        // into the embrace pose. World collision is unaffected so they don't
        // fall through the floor.
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
        player->data.angle.z = lockedPlayerYaw_;

        // Defer the Intro a few frames so the graph-var changes above settle
        // before the transition blend begins. Tick() drives the countdown.
        settleFramesRemaining_ = 3;

        SKSE::log::info("[CompositePairedAnimation] Settle queued; Intro deferred {} frames", settleFramesRemaining_);
        return true;
    }

    void RequestStop() {
        if (stage_ == Stage::Settle || stage_ == Stage::Intro || stage_ == Stage::Loop) {
            SKSE::log::info("[CompositePairedAnimation] RequestStop -> Exit stage");
            FireStageClips(pack_.exit, "Exit");
            stage_ = Stage::Exit;
            stageTimer_ = 0.0f;
        } else {
            SKSE::log::debug("[CompositePairedAnimation] RequestStop ignored (stage not interruptible)");
        }
    }

    // Called by FeedAnimState::MarkFeedEnded — teardown only (no re-notify).
    void OnComplete() {
        DoTeardown();
    }

    // External hard abort: teardown + notify feed state.
    void ForceStop() {
        SKSE::log::info("[CompositePairedAnimation] ForceStop");
        Finish();
    }

    void Tick(float delta) {
        if (!IsActive()) return;

        // Target validity. Death mid-Killing is the expected end of the kill;
        // death/loss in any other stage is an abort. Both route through Finish().
        auto ref = feedTargetHandle_.get();
        if (!ref) { Finish(); return; }
        auto* target = ref->As<RE::Actor>();
        if (!target || target->IsDead()) {
            Finish();
            return;
        }

        // Settle: frame countdown, then fire the Intro clips.
        if (stage_ == Stage::Settle) {
            if (settleFramesRemaining_ > 0) {
                --settleFramesRemaining_;
                if (settleFramesRemaining_ == 0) {
                    FireStageClips(pack_.intro, "Intro");
                    stage_ = Stage::Intro;
                    stageTimer_ = 0.0f;
                }
            }
            return;
        }

        stageTimer_ += delta;
        auto* settings = Settings::GetSingleton();

        switch (stage_) {
        case Stage::Intro: {
            if (stageTimer_ >= settings->NonCombat.CompositeIntroDuration) {
                FireStageClips(pack_.loop, "Loop");
                stage_ = Stage::Loop;
                stageTimer_ = 0.0f;
                // Seed the first gulp a randomized moment after the bite latches.
                gulpTimer_ = RandRange(settings->HealthDrain.GulpIntervalMin,
                                       settings->HealthDrain.GulpIntervalMax);
                // Feeding has truly begun — gate the centralized overhaul trigger
                // (fired in MarkFeedEnded). Stopping during Intro = no integration.
                FeedAnimState::MarkFeedEngaged();
            }
            break;
        }

        case Stage::Loop: {
            // The feeding loop plays indefinitely — it never auto-transitions to
            // Exit (only a player Stop does that, via RequestStop). The victim is
            // drained in randomized "gulps": every randomized interval a randomized
            // chunk of HP is removed (like sucking blood in mouthfuls). Gulps floor
            // at the lethal threshold so they never kill outright — once HP reaches
            // that floor we switch to the Kill (drained-dry) finisher.
            bool drainedDry = false;
            if (auto* av = target->AsActorValueOwner()) {
                const float max = av->GetPermanentActorValue(RE::ActorValue::kHealth);
                const float thr = std::clamp(settings->HealthDrain.GulpLethalThreshold, 0.0f, 1.0f);
                const float floorHP = max * thr;

                gulpTimer_ -= delta;
                if (gulpTimer_ <= 0.0f && max > 0.0f) {
                    const float cur = av->GetActorValue(RE::ActorValue::kHealth);
                    const float pct = RandRange(settings->HealthDrain.GulpPercentMin,
                                                settings->HealthDrain.GulpPercentMax);
                    const float newHP = std::max(floorHP, cur - max * (pct / 100.0f));
                    const float dmg = cur - newHP;
                    if (dmg > 0.0f) {
                        av->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -dmg);
                    }
                    // Schedule the next gulp at a randomized interval.
                    gulpTimer_ = RandRange(settings->HealthDrain.GulpIntervalMin,
                                           settings->HealthDrain.GulpIntervalMax);
                    SKSE::log::debug("[CompositePairedAnimation] Gulp: -{:.1f} HP ({:.1f}%), next in {:.2f}s",
                        dmg, pct, gulpTimer_);
                }

                if (max > 0.0f && av->GetActorValue(RE::ActorValue::kHealth) <= floorHP + 0.5f) {
                    drainedDry = true;
                }
            }

            if (drainedDry) {
                SKSE::log::info("[CompositePairedAnimation] Loop -> Killing (victim drained dry)");
                FeedAnimState::SetCurrentFeedLethal(true);
                FireStageClips(pack_.kill, "Kill");  // empty -> keeps loop clip visually
                stage_ = Stage::Killing;
                stageTimer_ = 0.0f;
            }
            break;
        }

        case Stage::Exit: {
            if (stageTimer_ >= settings->NonCombat.CompositeExitDuration) {
                SKSE::log::info("[CompositePairedAnimation] Exit complete - victim released");
                Finish();  // no kill
            }
            break;
        }

        case Stage::Killing: {
            if (stageTimer_ >= settings->NonCombat.CompositeKillDuration) {
                SKSE::log::info("[CompositePairedAnimation] Kill complete - draining victim dry");
                AnimUtil::KillTarget(target);  // kill while still posed/locked
                Finish();
            }
            break;
        }

        default:
            break;
        }
    }
}
