#include "PairedAnimation.h"
#include "utils/AnimUtil.h"
#include "feed/AnimationRegistry.h"
#include "feed/FeedAnimState.h"
#include "Settings.h"

namespace PairedAnimation {
    // Private state - hidden from header, stored as ObjectRefHandle for memory safety
    // THREADING: Must only be accessed from main game thread (ensured via SKSE::GetTaskInterface()->AddTask)
    static RE::ObjectRefHandle feedTargetHandle_{};

    // Fallback key for ExitFeedState: pacify/restraint are FormID-keyed
    // serialized state that must be released even after the handle dies.
    static RE::FormID feedTargetFormID_ = 0;

    // Track weapon/magic drawn state before feeding so we can restore it afterwards
    static bool wasWeaponDrawn_ = false;

    // Track if target was already dead when feed started (for dead feed counter)
    static bool wasTargetDeadAtStart_ = false;

    // Set feed target - stores ObjectRefHandle for safe persistence
    void SetFeedTarget(RE::Actor* target) {
        if (target) {
            feedTargetHandle_ = target->GetHandle();
            feedTargetFormID_ = target->GetFormID();
        } else {
            feedTargetHandle_ = {};
            feedTargetFormID_ = 0;
        }
    }

    // Keeps feedTargetFormID_: failure paths clear the target before the late
    // ExitFeedState that still needs it to release restraint. Stale is safe -
    // release is a no-op on an unrestrained actor.
    void ClearFeedTarget() {
        feedTargetHandle_ = {};
    }

    // Get current feed target - safely validates handle before returning
    // Returns NiPointer to keep reference alive in caller's scope
    RE::NiPointer<RE::Actor> GetFeedTarget() {
        // Resolve the handle - .get() checks if object is loaded and valid
        auto ref = feedTargetHandle_.get();

        if (!ref) {
            // Actor no longer exists, is deleted, or unloaded
            feedTargetHandle_ = {};
            return nullptr;
        }

        // Cast to Actor (ObjectRefHandle stores TESObjectREFR, we need Actor)
        auto* actor = ref->As<RE::Actor>();

        // Perform gameplay logic checks
        if (!actor || actor->IsDead() || actor->IsDisabled()) {
            feedTargetHandle_ = {}; // Clear handle if target is invalid for gameplay
            return nullptr;
        }

        // Return smart pointer - keeps reference alive in caller's scope
        return RE::NiPointer<RE::Actor>(actor);
    }

    // Check if player is on left or right side of target (for bed animations)
    bool IsPlayerOnLeftSide(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return true;

        auto playerPos = player->GetPosition();
        auto targetPos = target->GetPosition();
        float targetHeading = target->GetAngleZ();

        float dx = playerPos.x - targetPos.x;
        float dy = playerPos.y - targetPos.y;

        // Cross product to determine side relative to target's facing
        float cross = std::cos(targetHeading) * dx - std::sin(targetHeading) * dy;
        return cross > 0;
    }

    // Check if furniture is a bedroll (vs regular bed) - case insensitive
    bool IsBedroll(RE::TESObjectREFR* furniture) {
        if (!furniture) return false;

        auto baseObj = furniture->GetBaseObject();
        if (!baseObj) return false;

        auto editorID = baseObj->GetFormEditorID();
        if (editorID) {
            std::string lower(editorID);
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return lower.find("bedroll") != std::string::npos;
        }
        return false;
    }

    // Play feed animation by EditorID
    // For standing/combat: paired animation with target
    // For bed/bedroll: solo idle on player only (no target involvement)
    // callback is invoked on game thread after animation starts (or fails)
    void PlayPairedFeed(const char* idleEditorID, RE::Actor* target, bool isPaired,
                        FeedCallback callback) {
        SKSE::log::debug("[PairedAnimation] PlayPairedFeed called (paired={})", isPaired);

        // Validate inputs
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("[PairedAnimation] FAILED: player is null");
            if (callback) {
                callback(false, nullptr);
            }
            return;
        }

        // Save weapon/magic drawn state so we can restore it after feeding
        auto* playerState = player->AsActorState();
        wasWeaponDrawn_ = playerState && playerState->IsWeaponDrawn();
        SKSE::log::info("[PairedAnimation] Saved weapon drawn state: {}", wasWeaponDrawn_);
        if (!idleEditorID) {
            SKSE::log::error("[PairedAnimation] FAILED: idleEditorID is null");
            if (callback) {
                callback(false, target);
            }
            return;
        }
        // Target only required for paired animations
        if (isPaired && !target) {
            SKSE::log::error("[PairedAnimation] FAILED: target is null for paired animation");
            if (callback) {
                callback(false, nullptr);
            }
            return;
        }

        SKSE::log::debug("[PairedAnimation] Looking up idle: '{}'", idleEditorID);
        if (target) {
            SKSE::log::debug("[PairedAnimation] Target: {} (FormID: {:X})", target->GetName(), target->GetFormID());
        }

        SetFeedTarget(target);

        // Track if target was already dead (for dead feed counter - don't count lethal kills)
        wasTargetDeadAtStart_ = target && target->IsDead();

        // Lookup idle form
        auto* feedIdle = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(idleEditorID);
        if (!feedIdle) {
            SKSE::log::error("[PairedAnimation] FAILED: Idle '{}' not found in game data", idleEditorID);
            ClearFeedTarget();
            if (callback) {
                callback(false, target);
            }
            return;
        }
        SKSE::log::debug("[PairedAnimation] Found idle form: {:X}", feedIdle->GetFormID());

        // Use AnimUtil::playIdle for thread-safe, handle-based animation playback
        SKSE::log::debug("[PairedAnimation] Calling AnimUtil::playIdle (paired={})...", isPaired);

        // Solo animations: sheathe weapon first if drawn. Paired animations handle weapon state natively.
        if (!isPaired && wasWeaponDrawn_) {
            SKSE::log::info("[PairedAnimation] Solo animation - sheathing weapon");
            player->DrawWeaponMagicHands(false);
        }

        // AnimUtil::playIdle handles thread-safety and ObjectRefHandle automatically
        // The callback will be invoked on game thread after PlayIdle succeeds or fails
        // Always pass target for callback (needed for integration) even for solo animations
        AnimUtil::playIdle(player, feedIdle, target, callback, isPaired);

        SKSE::log::info("[PairedAnimation] Animation playback initiated (callback pending)");
    }

    // Force stop - recovery function for stuck animations
    void ForceStop() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            if (auto* process = player->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(player, true);
            }

            // // Restore weapon/magic drawn state if it was drawn before feeding
            // if (wasWeaponDrawn_) {
            //     SKSE::log::info("[PairedAnimation] ForceStop: Restoring weapon drawn state");
            //     AnimUtil::redrawWeapon(player);
            //     wasWeaponDrawn_ = false;
            // }
        }
        // Safely retrieve target actor via handle (returns smart pointer)
        if (auto targetPtr = GetFeedTarget()) {
            if (auto* process = targetPtr->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(targetPtr.get(), true);
            }
        }
        ClearFeedTarget();
        SKSE::log::info("[PairedAnimation] ForceStop called");
    }

    void ResetForLoad() {
        feedTargetHandle_ = {};
        feedTargetFormID_ = 0;
        wasWeaponDrawn_ = false;
        wasTargetDeadAtStart_ = false;
    }

    // Called when feed ends normally - restores player control
    void OnComplete() {
        SKSE::log::debug("[PairedAnimation] OnComplete");

        // Undo per-actor mutations applied by EnterFeedState. Resolves target
        // via feedTargetHandle_, so this must run before ClearFeedTarget() below.
        ExitFeedState();

        // Increment dead feed count only if target was already dead when feed started
        // Don't count lethal feeds that killed the target
        if (wasTargetDeadAtStart_) {
            auto ref = feedTargetHandle_.get();
            if (ref) {
                auto* actor = ref->As<RE::Actor>();
                if (actor) {
                    AnimUtil::IncrementDeadFeedCount(actor);
                }
            }
        }
        wasTargetDeadAtStart_ = false;

        // Restore weapon drawn state if we sheathed it for a solo animation
        if (wasWeaponDrawn_) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                SKSE::log::info("[PairedAnimation] OnComplete: Redrawing weapon");
                player->DrawWeaponMagicHands(true);
            }
            wasWeaponDrawn_ = false;
        }

        ClearFeedTarget();
    }

    void EnterFeedState(const FeedStateContext& ctx) {
        if (!ctx.player || !ctx.target) {
            SKSE::log::warn("[PairedAnimation::EnterFeedState] null actor (player={}, target={})",
                ctx.player ? "ok" : "null", ctx.target ? "ok" : "null");
            return;
        }

        // 1. Clear engine-level animation blockers BEFORE applying new state -
        //    otherwise residual stagger/attack/knockdown can fight positioning
        //    and PlayIdle may silently no-op.
        AnimUtil::FlushAnimationGraph(ctx.player);
        AnimUtil::FlushAnimationGraph(ctx.target);

        // 2. Kill-move flag - blocks Quick Loot and other mod interference.
        AnimUtil::SetInKillMove(ctx.player, true);
        AnimUtil::SetInKillMove(ctx.target, true);

        // 3. Pacify target if either actor is in combat (prevents attack interruption).
        if (ctx.playerInCombat || ctx.targetInCombat) {
            AnimUtil::PacifyActor(ctx.target);
        }

        // 4. Positioning - only for upright targets, gated by settings.
        const bool isUpright = (ctx.targetState == Feed::kStanding || ctx.targetState == Feed::kCombat);
        if (isUpright) {
            if (ctx.enableHeightAdjust) {
                auto playerPos = ctx.player->GetPosition();
                auto targetPos = ctx.target->GetPosition();
                float heightDiff = std::fabs(targetPos.z - playerPos.z);
                SKSE::log::info("Height check BEFORE adjustment: player Z={:.2f}, target Z={:.2f}, diff={:.2f}",
                    playerPos.z, targetPos.z, heightDiff);

                AnimUtil::ApplyHeightAdjustment(ctx.player, ctx.target, ctx.minHeightDiff, ctx.maxHeightDiff);

                playerPos = ctx.player->GetPosition();
                targetPos = ctx.target->GetPosition();
                heightDiff = std::fabs(targetPos.z - playerPos.z);
                SKSE::log::info("Height check AFTER adjustment: player Z={:.2f}, target Z={:.2f}, diff={:.2f}",
                    playerPos.z, targetPos.z, heightDiff);
            }

            if (ctx.enableRotation) {
                AnimUtil::RotateTargetToClosest(ctx.target, ctx.player);
                AnimUtil::RotateAttackerToTarget(ctx.player, ctx.target);
            }
        }

        // 5. Graph vars LAST - OAR must see the feedType variant before PlayIdle fires.
        AnimUtil::SetFeedGraphVars(ctx.player, ctx.feedType);
        AnimUtil::SetFeedGraphVars(ctx.target, ctx.feedType);
    }

    void ExitFeedState() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::Actor* target = nullptr;
        if (auto ref = feedTargetHandle_.get()) {
            target = ref->As<RE::Actor>();
        }

        // LIFO of EnterFeedState.
        // 1. Clear graph vars first so animation graph returns to default state.
        if (target) AnimUtil::ClearFeedGraphVars(target);
        if (player) AnimUtil::ClearFeedGraphVars(player);

        // 2. Release pacify (no-op if target was never pacified). If the
        //    handle died mid-feed, release by FormID - the restrained flag is
        //    serialized and would otherwise persist into every later save.
        if (target) {
            AnimUtil::UndoPacifyActor(target);
        } else if (feedTargetFormID_) {
            SKSE::log::warn("[PairedAnimation::ExitFeedState] Dead target handle - releasing pacify/restraint via FormID {:X}",
                feedTargetFormID_);
            AnimUtil::UndoPacifyActor(feedTargetFormID_);
        }

        // 3. Kill-move flag last - QuickLoot etc. re-enable after all other state settled.
        if (player) AnimUtil::SetInKillMove(player, false);
        if (target) AnimUtil::SetInKillMove(target, false);
    }

    void ExecuteFeed(const char* idleEditorID, RE::Actor* target, bool isPairedAnim, bool isLethal, bool hasOARAnimation) {
        auto* settings = Settings::GetSingleton();

        // if (settings->NonCombat.UseCompositePairedAnimation && isPairedAnim) {
        //     SKSE::log::info("Using composite paired animation mode");
        //     // temporary disabled
        //     // if (CompositePairedAnimation::Play(target)) {
        //     //     PapyrusCall::SendOnVampireFeedEvent(target);
        //     //     auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
        //     //     if (vampireQuest) {
        //     //         PapyrusCall::CallVampireFeed(vampireQuest, target);
        //     //     } else {
        //     //         SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
        //     //     }
        //     //     return;
        //     // }
        //     SKSE::log::warn("Composite paired feed failed, falling back to native paired animation");
        // }


        SKSE::log::info("Playing feed idle '{}' (paired={}, lethal={})", idleEditorID, isPairedAnim, isLethal);

        // Create callback that runs AFTER animation starts successfully (on game thread)
        // This ensures integration logic runs only when animation is actually playing
        auto onAnimationResult = [](bool success, RE::Actor* /*callbackTarget*/) {
            if (!success) {
                SKSE::log::warn("PairedAnimation failed - animation did not start");
                // End the feed centrally (guarded against double-fire): runs
                // ExitFeedState so a pacified/restrained target is released
                // and the save block lifts.
                FeedAnimState::MarkFeedEnded();
                return;
            }
            // Animation is actually playing: mark the feed engaged. The vampire-
            // overhaul integration now fires centrally in MarkFeedEnded() once the
            // feed completes (see FeedIntegration::Run).
            SKSE::log::info("Animation started successfully - feed engaged");
            FeedAnimState::MarkFeedEngaged();
        };

        // PlayPairedFeed now takes callback - integration runs after animation starts
        PairedAnimation::PlayPairedFeed(idleEditorID, target, isPairedAnim, onAnimationResult);
    }
}
