#include "PCH.h"
#include "feed/FeedAnimState.h"
#include "feed/PairedAnimPromptSink.h"
#include "feed/PairedAnimation.h"
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

    void MarkFeedStarted() {
        feedState.store(State::Active, std::memory_order_release);
        currentFeedLethal.store(false, std::memory_order_release);
        vfdTriggerCount.store(0, std::memory_order_release);
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
        SKSE::log::info("========== FEED ENDED ==========");

        // Always reset time multiplier to normal (safe even if not slowed)
        auto* timer = RE::BSTimer::GetSingleton();
        if (timer) {
            timer->SetGlobalTimeMultiplier(1.0f, true);
        }

        // Clear the active feed target (thread-safe). Per-actor cleanup
        // (kill-move, graph vars, pacify) is owned by PairedAnimation::ExitFeedState,
        // called from OnComplete below.
        PairedAnimPromptSink::GetSingleton()->SetActiveFeedTarget(nullptr);

        PairedAnimation::OnComplete();
        // disable as require more refactor
        //CompositePairedAnimation::OnComplete();
        PairedAnimPromptSink::GetSingleton()->RefreshPrompt();
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
}
