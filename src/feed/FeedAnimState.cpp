#include "PCH.h"
#include "feed/FeedAnimState.h"
#include "feed/FeedPromptSink.h"
#include "feed/PairedAnimation.h"
#include "feed/CompositePairedAnimation.h"
#include "integration/FeedIntegration.h"
#include "feed/FeedHealthBarOverlay.h"
#include "feed/WitnessDetection.h"
#include "Settings.h"
#include <atomic>

namespace FeedAnimState {
    // Single atomic state enum prevents race conditions between coupled states
    enum class State {
        Idle,     // No feed active
        Active,   // Feed in progress
        Ended     // Feed just ended (needs acknowledgment)
    };

    std::atomic<State> feedState{State::Idle};
    std::atomic<bool> currentFeedLethal{false};
    std::atomic<uint32_t> vfdTriggerCount{0};
    std::atomic<bool> feedEngaged{false};
    std::atomic<bool> feedHasOAR{false};
    std::atomic<bool> feedInCombat{false};
    std::atomic<bool> feedSleeping{false};
    std::atomic<bool> feedStartNotified{false};

    namespace {
        // Saving is blocked for the whole feed window: the victim's restrained
        // flag (Actor.SetRestrained) is serialized, so a mid-feed save would
        // brick the NPC in that timeline.
        void SetSaveBlock(bool block) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;
            auto& flags = player->GetPlayerRuntimeData().byCharGenFlag;
            if (block) {
                flags.set(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
            } else {
                flags.reset(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
            }
        }
    }

    void MarkFeedStarted() {
        // Clear the witness "already reported" latch BEFORE publishing Active, so a witness
        // check that observes the new feed (acquire-load of feedState) also observes the
        // cleared latch (it is sequenced before the release-store below).
        WitnessDetection::ResetFeedReport();  // new feed -> allow exactly one assault charge
        feedState.store(State::Active, std::memory_order_release);
        currentFeedLethal.store(false, std::memory_order_release);
        vfdTriggerCount.store(0, std::memory_order_release);
        feedEngaged.store(false, std::memory_order_release);
        feedHasOAR.store(false, std::memory_order_release);
        feedInCombat.store(false, std::memory_order_release);
        feedSleeping.store(false, std::memory_order_release);
        feedStartNotified.store(false, std::memory_order_release);
        SetSaveBlock(true);  // before any restrain can land
        SKSE::log::info("========== FEED STARTED ==========");

        // Apply time slowdown if enabled and player is in combat
        auto* settings = Settings::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        bool isCombat = player && player->IsInCombat();

        if (settings->Animation.EnableTimeSlowdown && isCombat) {
            auto* timer = RE::BSTimer::GetSingleton();
            if (timer) {
                timer->SetGlobalTimeMultiplier(settings->Animation.TimeSlowdownMultiplier, true);
                SKSE::log::info("Combat feed time slowdown applied: {}x", settings->Animation.TimeSlowdownMultiplier);
            }
        }
    }

    void MarkFeedEnded() {
        // currentFeedLethal / vfdTriggerCount are reset in MarkFeedStarted for the next feed;
        // leaving them set here is harmless (gated by feedState) and matches killMoveStartSeen's pattern.
        feedState.store(State::Ended, std::memory_order_release);
        SetSaveBlock(false);
        SKSE::log::info("========== FEED ENDED ==========");

        // Always reset time multiplier to normal (safe even if not slowed)
        auto* timer = RE::BSTimer::GetSingleton();
        if (timer) {
            timer->SetGlobalTimeMultiplier(1.0f, true);
        }

        // Snapshot the victim before the active target is cleared below; reused for
        // both the overhaul integration and the confidence-based witness reaction.
        auto victim = FeedPromptSink::GetSingleton()->GetActiveFeedTarget();

        // Fire the vampire-overhaul integration ONCE for both paths, now that the feed's
        // final lethality is known. feedEngaged skips aborted feeds and is read-and-cleared
        // so a double MarkFeedEnded (timeout + event) can't double-fire. Runs BEFORE the
        // active target is cleared below. Composite already sent the narrative events at
        // Loop start, so Run() applies only the mechanical effects; legacy sends both here.
        if (ConsumeFeedEngaged()) {
            if (victim) {
                FeedIntegration::Run(victim.get(), IsCurrentFeedLethal(), GetFeedHasOAR());
            } else {
                SKSE::log::warn("MarkFeedEnded: feed engaged but no active target for integration");
            }
        }

        // Hide the victim's health bar (idempotent; no-op if none shown).
        FeedHealthBarOverlay::GetSingleton()->Hide();

        // Clear the active feed target (thread-safe). Per-actor cleanup
        // (kill-move, graph vars, pacify) is owned by PairedAnimation::ExitFeedState,
        // called from OnComplete below.
        FeedPromptSink::GetSingleton()->SetActiveFeedTarget(nullptr);

        PairedAnimation::OnComplete();
        CompositePairedAnimation::OnComplete();

        // After teardown released the restraint, settle the victim's own reaction: it reports the
        // feed as a crime (deferred here so a drained-dry victim, now dead, files no bounty) and,
        // if it turns hostile under the 3-tier model, starts combat. Deferred so the AI reset
        // settles first. Bystanders were already handled live during the feed.
        WitnessDetection::ApplyWitnessReactions(RE::PlayerCharacter::GetSingleton(), victim.get());

        FeedPromptSink::GetSingleton()->RefreshPrompt();
    }

    bool CheckAndClearFeedEnded() {
        // Atomically check if ended and transition to idle
        State expected = State::Ended;
        bool wasEnded = feedState.compare_exchange_strong(
            expected, State::Idle,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        );
        return wasEnded;
    }

    bool IsFeedActive() {
        return feedState.load(std::memory_order_acquire) == State::Active;
    }

    std::atomic<bool> killMoveStartSeen{false};

    void MarkKillMoveStartSeen() {
        killMoveStartSeen.store(true, std::memory_order_release);
    }

    bool ConsumeKillMoveStart() {
        return killMoveStartSeen.exchange(false, std::memory_order_acq_rel);
    }

    void ResetKillMoveStart() {
        killMoveStartSeen.store(false, std::memory_order_release);
    }

    void ResetForLoad() {
        feedState.store(State::Idle, std::memory_order_release);
        currentFeedLethal.store(false, std::memory_order_release);
        vfdTriggerCount.store(0, std::memory_order_release);
        feedEngaged.store(false, std::memory_order_release);
        feedHasOAR.store(false, std::memory_order_release);
        feedInCombat.store(false, std::memory_order_release);
        feedSleeping.store(false, std::memory_order_release);
        feedStartNotified.store(false, std::memory_order_release);
        killMoveStartSeen.store(false, std::memory_order_release);

        // A new session must never inherit a save block from the previous one.
        SetSaveBlock(false);

        // Undo a lingering combat-feed slowdown; idempotent if none was applied.
        if (auto* timer = RE::BSTimer::GetSingleton()) {
            timer->SetGlobalTimeMultiplier(1.0f, true);
        }
    }

    void SetCurrentFeedLethal(bool lethal) {
        currentFeedLethal.store(lethal, std::memory_order_release);
    }

    bool IsCurrentFeedLethal() {
        return currentFeedLethal.load(std::memory_order_acquire);
    }

    uint32_t IncrementVFDTriggerCount() {
        return vfdTriggerCount.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    void MarkFeedEngaged() {
        feedEngaged.store(true, std::memory_order_release);
    }

    bool ConsumeFeedEngaged() {
        return feedEngaged.exchange(false, std::memory_order_acq_rel);
    }

    bool HasFeedEngaged() {
        return feedEngaged.load(std::memory_order_acquire);
    }

    void SetFeedHasOAR(bool hasOAR) {
        feedHasOAR.store(hasOAR, std::memory_order_release);
    }

    bool GetFeedHasOAR() {
        return feedHasOAR.load(std::memory_order_acquire);
    }

    void SetFeedInCombat(bool inCombat) {
        feedInCombat.store(inCombat, std::memory_order_release);
    }

    bool GetFeedInCombat() {
        return feedInCombat.load(std::memory_order_acquire);
    }

    void SetFeedSleeping(bool sleeping) {
        feedSleeping.store(sleeping, std::memory_order_release);
    }

    bool GetFeedSleeping() {
        return feedSleeping.load(std::memory_order_acquire);
    }

    void SetFeedStartNotified(bool notified) {
        feedStartNotified.store(notified, std::memory_order_release);
    }

    bool GetFeedStartNotified() {
        return feedStartNotified.load(std::memory_order_acquire);
    }
}
