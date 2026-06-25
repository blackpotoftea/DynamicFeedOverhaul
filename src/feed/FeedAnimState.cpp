#include "PCH.h"
#include "feed/FeedAnimState.h"
#include "feed/FeedPromptSink.h"
#include "feed/PairedAnimation.h"
#include "feed/CompositePairedAnimation.h"
#include "feed/FeedHealthBarOverlay.h"
#include "feed/TargetState.h"
#include "Settings.h"
#include "papyrus/PapyrusCall.h"
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

    void MarkFeedStarted() {
        feedState.store(State::Active, std::memory_order_release);
        currentFeedLethal.store(false, std::memory_order_release);
        vfdTriggerCount.store(0, std::memory_order_release);
        feedEngaged.store(false, std::memory_order_release);
        feedHasOAR.store(false, std::memory_order_release);
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

    // Confidence-based reaction to a feed the victim witnessed. Runs at feed end,
    // AFTER teardown has released the victim's restraint. An awake, surviving victim
    // necessarily saw their own assault; if brave/foolhardy (Confidence >= threshold)
    // they are put into combat against the player (assault model). Cowardly/cautious
    // victims keep only the silent bounty applied during the feed (theft model) and
    // may flee naturally via the assault alarm. Skipped for the dead, the
    // asleep/unconscious (didn't witness it), and followers (won't turn on the player).
    static void TriggerWitnessReaction(RE::NiPointer<RE::Actor> victim) {
        auto* settings = Settings::GetSingleton();
        if (!settings->Combat.EnableWitnessCombatReaction) return;

        auto* v = victim.get();
        if (!v || v->IsDead() || v->IsDisabled()) return;
        if (v->IsPlayerTeammate()) return;                  // followers don't turn on you
        if (!TargetState::IsConsciousAndAware(v)) return;   // asleep/unconscious = didn't witness it

        const int conf = static_cast<int>(TargetState::GetConfidence(v));
        if (conf < settings->Combat.AssaultConfidenceThreshold) {
            SKSE::log::info("[WitnessReaction] {} confidence {} < threshold {} - theft model (bounty only, no attack)",
                v->GetName(), conf, settings->Combat.AssaultConfidenceThreshold);
            return;
        }

        // Defer a frame so the teardown's restraint release / AI refresh settles
        // before combat starts, otherwise StartCombat can fizzle against a still-
        // restrained actor.
        auto handle = v->CreateRefHandle();
        SKSE::GetTaskInterface()->AddTask([handle] {
            auto ref = handle.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor || actor->IsDead()) return;
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;
            SKSE::log::info("[WitnessReaction] {} is brave enough - starting combat against player", actor->GetName());
            PapyrusCall::StartCombat(actor, player);
        });
    }

    void MarkFeedEnded() {
        // currentFeedLethal / vfdTriggerCount are reset in MarkFeedStarted for the next feed;
        // leaving them set here is harmless (gated by feedState) and matches killMoveStartSeen's pattern.
        feedState.store(State::Ended, std::memory_order_release);
        SKSE::log::info("========== FEED ENDED ==========");

        // Always reset time multiplier to normal (safe even if not slowed)
        auto* timer = RE::BSTimer::GetSingleton();
        if (timer) {
            timer->SetGlobalTimeMultiplier(1.0f, true);
        }

        // Snapshot the victim before the active target is cleared below; reused for
        // both the overhaul integration and the confidence-based witness reaction.
        auto victim = FeedPromptSink::GetSingleton()->GetActiveFeedTarget();

        // Centralized vampire-overhaul trigger: fire ONCE here for both the
        // legacy and composite paths, now that the feed is actually done.
        // Gated on feedEngaged (skips aborted feeds) and read-and-cleared so a
        // double MarkFeedEnded (timeout + event) can't double-fire. Runs BEFORE
        // the active target is cleared below. isLethal/hasOAR are the per-feed
        // context stashed at start (composite flips lethal true on the Kill stage).
        if (ConsumeFeedEngaged()) {
            if (victim) {
                PairedAnimation::RunFeedIntegration(victim.get(), IsCurrentFeedLethal(), GetFeedHasOAR());
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

        // After teardown released the restraint: a witnessed brave victim now fights
        // back (deferred so the AI reset settles first); timid victims keep the bounty.
        TriggerWitnessReaction(victim);

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

    void SetFeedHasOAR(bool hasOAR) {
        feedHasOAR.store(hasOAR, std::memory_order_release);
    }

    bool GetFeedHasOAR() {
        return feedHasOAR.load(std::memory_order_acquire);
    }
}
