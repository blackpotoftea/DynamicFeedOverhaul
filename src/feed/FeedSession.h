#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <RE/Skyrim.h>

namespace FeedSession {

    //=========================================================================
    // STATE
    //=========================================================================

    enum class State {
        Idle,       // No feed session active
        Starting,   // Setup in progress
        Playing,    // Animation playing successfully
        Retrying,   // PlayIdle failed, attempting retry (placeholder for future)
        Ending,     // Animation ended, cleanup in progress
        Failed      // Session failed
    };

    //=========================================================================
    // CONFIGURATION (placeholder for future retry)
    //=========================================================================

    struct RetryConfig {
        int maxAttempts = 1;        // Currently disabled (1 = no retry)
        int retryDelayMs = 50;      // Delay between retries when enabled
        bool retryOnPlayIdleFalse = false;  // Disabled for now
    };

    //=========================================================================
    // SETUP TRACKING
    //=========================================================================

    // Track what was set up for precise cleanup
    struct SetupFlags {
        bool playerKillMoveSet = false;
        bool targetKillMoveSet = false;
        bool targetPacified = false;
        bool graphVarsSet = false;
        bool eventSinkRegistered = false;
        bool timeSlowdownApplied = false;
    };

    //=========================================================================
    // SESSION DATA
    //=========================================================================

    struct SessionData {
        RE::ObjectRefHandle playerHandle;
        RE::ObjectRefHandle targetHandle;
        RE::FormID idleFormID = 0;

        bool isPaired = true;
        int feedType = 0;

        // Retry tracking (placeholder)
        int attemptNumber = 0;
        std::chrono::steady_clock::time_point startTime;
    };

    //=========================================================================
    // CALLBACKS
    //=========================================================================

    // Called when animation successfully starts
    using OnAnimationStarted = std::function<void(RE::Actor* target)>;

    // Called when session ends (success or failure)
    using OnSessionComplete = std::function<void(bool success, RE::Actor* target)>;

    //=========================================================================
    // PRIMARY API
    //=========================================================================

    /// Start a new feed session. Returns false if session already active.
    /// @param player       Player actor (must be valid)
    /// @param target       Target actor (must be valid for paired, can be null for solo)
    /// @param idle         Idle form to play
    /// @param isPaired     True for paired animation, false for solo
    /// @param feedType     Animation feed type ID for graph variables
    /// @param onStarted    Called on game thread when animation successfully starts
    /// @param onComplete   Called on game thread when session ends
    bool Start(
        RE::Actor* player,
        RE::Actor* target,
        RE::TESIdleForm* idle,
        bool isPaired,
        int feedType,
        OnAnimationStarted onStarted = nullptr,
        OnSessionComplete onComplete = nullptr
    );

    /// Check if a session is currently active
    bool IsActive();

    //=========================================================================
    // ANIMATION EVENT HANDLERS
    //=========================================================================

    /// Called when PairEnd/IdleStop animation event is received
    void OnAnimationEnded();

    /// Check and handle timeout (called from periodic update)
    void CheckTimeout();

    //=========================================================================
    // STATE QUERIES (for external systems)
    //=========================================================================

    /// Get active feed target (for witness detection, etc.)
    RE::NiPointer<RE::Actor> GetActiveFeedTarget();

    /// Check if feed just ended (for prompt refresh logic)
    /// Atomically checks and clears the "ended" flag
    bool CheckAndClearFeedEnded();

    /// Check if KillMoveStart was detected after PlayIdle, clears waiting flag
    /// Returns true if was waiting for KillMoveStart (logs elapsed time)
    bool CheckKillMoveStartDetected();

}  // namespace FeedSession
