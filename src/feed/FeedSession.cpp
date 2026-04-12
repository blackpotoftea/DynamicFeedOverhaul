#include "PCH.h"
#include "feed/FeedSession.h"
#include "feed/PairedAnimPromptSink.h"
#include "feed/CustomFeed.h"
#include "utils/AnimUtil.h"
#include "Settings.h"

namespace FeedSession {

    //=========================================================================
    // INTERNAL STATE
    //=========================================================================

    namespace {
        // Session state
        std::atomic<State> g_State{State::Idle};
        std::mutex g_SessionMutex;

        // Current session data (protected by g_SessionMutex)
        SessionData g_Session;
        SetupFlags g_SetupFlags;

        // Callbacks (protected by g_SessionMutex)
        OnAnimationStarted g_OnStarted;
        OnSessionComplete g_OnComplete;

        // For CheckAndClearFeedEnded pattern (prompt refresh)
        std::atomic<bool> g_FeedEndedFlag{false};

        // KillMoveStart detection tracking
        std::atomic<bool> g_WaitingForKillMoveStart{false};
        std::chrono::steady_clock::time_point g_PlayIdleTime{};

        // Configuration (can be made configurable later)
        RetryConfig g_RetryConfig;
    }

    //=========================================================================
    // INTERNAL HELPERS
    //=========================================================================

    namespace {

        // Forward declarations
        void AttemptPlayIdle();
        void CleanupSession(const std::string& reason);
        void OnTimeout();

        // Cleanup based on SetupFlags - undoes exactly what was set up
        void CleanupSession(const std::string& reason) {
            SKSE::log::info("[FeedSession] CleanupSession: {}", reason);

            // Capture data under lock
            SetupFlags flags;
            SessionData session;
            OnSessionComplete completionCallback;
            bool wasPlaying = false;

            {
                std::lock_guard<std::mutex> lock(g_SessionMutex);
                flags = g_SetupFlags;
                session = g_Session;
                completionCallback = std::move(g_OnComplete);
                wasPlaying = (g_State.load() == State::Playing);
            }

            // Perform cleanup on main thread
            auto playerHandle = session.playerHandle;
            auto targetHandle = session.targetHandle;

            SKSE::GetTaskInterface()->AddTask([=]() mutable {
                SKSE::log::info("========== FEED ENDED ==========");

                // Reset time multiplier first (safe even if not slowed)
                if (flags.timeSlowdownApplied) {
                    if (auto* timer = RE::BSTimer::GetSingleton()) {
                        timer->SetGlobalTimeMultiplier(1.0f, true);
                        SKSE::log::debug("[FeedSession] Time multiplier reset to 1.0");
                    }
                }

                // Clear kill move flags
                if (flags.playerKillMoveSet) {
                    if (auto ref = playerHandle.get()) {
                        if (auto* player = ref->As<RE::Actor>()) {
                            AnimUtil::SetInKillMove(player, false);
                            SKSE::log::debug("[FeedSession] Cleared player kill move flag");
                        }
                    }
                }

                if (flags.targetKillMoveSet) {
                    if (auto ref = targetHandle.get()) {
                        if (auto* target = ref->As<RE::Actor>()) {
                            AnimUtil::SetInKillMove(target, false);
                            SKSE::log::debug("[FeedSession] Cleared target kill move flag");
                        }
                    }
                }

                // Undo pacify
                if (flags.targetPacified) {
                    if (auto ref = targetHandle.get()) {
                        if (auto* target = ref->As<RE::Actor>()) {
                            AnimUtil::UndoPacifyActor(target);
                            SKSE::log::debug("[FeedSession] Released target from pacify");
                        }
                    }
                }

                // Unregister animation event sink
                if (flags.eventSinkRegistered) {
                    AnimEventSink::Unregister();
                    SKSE::log::debug("[FeedSession] Unregistered animation event sink");
                }

                // Call CustomFeed::OnComplete for dead feed counter etc.
                CustomFeed::OnComplete();

                // Resolve target for callback
                RE::Actor* targetActor = nullptr;
                if (auto ref = targetHandle.get()) {
                    targetActor = ref->As<RE::Actor>();
                }

                // Invoke completion callback
                if (completionCallback) {
                    completionCallback(wasPlaying, targetActor);
                }

                // Refresh prompt
                PairedAnimPromptSink::GetSingleton()->RefreshPrompt();
            });

            // Reset state
            {
                std::lock_guard<std::mutex> lock(g_SessionMutex);
                g_Session = SessionData{};
                g_SetupFlags = SetupFlags{};
                g_OnStarted = nullptr;
                g_OnComplete = nullptr;
            }

            // Check if KillMoveStart was never detected
            if (g_WaitingForKillMoveStart.exchange(false)) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - g_PlayIdleTime).count();
                SKSE::log::warn("[FeedSession] KillMoveStart NOT detected ({}ms elapsed)", elapsed);
            }

            // Set ended flag for prompt refresh logic
            g_FeedEndedFlag.store(true);
            g_State.store(State::Idle);
        }

        // Attempt to play the idle animation
        void AttemptPlayIdle() {
            SessionData session;
            OnAnimationStarted onStarted;

            {
                std::lock_guard<std::mutex> lock(g_SessionMutex);
                session = g_Session;
                session.attemptNumber++;
                g_Session.attemptNumber = session.attemptNumber;
                onStarted = g_OnStarted;
            }

            SKSE::log::info("[FeedSession] PlayIdle attempt {}/{}",
                session.attemptNumber, g_RetryConfig.maxAttempts);

            // Validate handles
            auto playerRef = session.playerHandle.get();
            if (!playerRef) {
                SKSE::log::error("[FeedSession] Player handle invalid");
                g_State.store(State::Failed);
                CleanupSession("Player handle invalid");
                return;
            }

            auto* player = playerRef->As<RE::Actor>();
            if (!player) {
                SKSE::log::error("[FeedSession] Player cast failed");
                g_State.store(State::Failed);
                CleanupSession("Player cast failed");
                return;
            }

            RE::Actor* target = nullptr;
            if (session.targetHandle) {
                auto targetRef = session.targetHandle.get();
                if (session.isPaired && !targetRef) {
                    SKSE::log::error("[FeedSession] Target handle invalid for paired animation");
                    g_State.store(State::Failed);
                    CleanupSession("Target handle invalid");
                    return;
                }
                if (targetRef) {
                    target = targetRef->As<RE::Actor>();
                }
            }

            // Lookup idle form
            auto* idle = RE::TESForm::LookupByID<RE::TESIdleForm>(session.idleFormID);
            if (!idle) {
                SKSE::log::error("[FeedSession] Idle form {:X} not found", session.idleFormID);
                g_State.store(State::Failed);
                CleanupSession("Idle form not found");
                return;
            }

            // Create PlayIdle callback
            auto playIdleCallback = [onStarted, attemptNum = session.attemptNumber](bool success, RE::Actor* callbackTarget) {
                if (success) {
                    g_State.store(State::Playing);
                    SKSE::log::info("[FeedSession] Animation started successfully on attempt {}", attemptNum);

                    // Invoke start callback (integration logic)
                    if (onStarted) {
                        onStarted(callbackTarget);
                    }
                } else {
                    // Check if we should retry (currently disabled)
                    if (g_RetryConfig.retryOnPlayIdleFalse &&
                        attemptNum < g_RetryConfig.maxAttempts) {

                        g_State.store(State::Retrying);
                        SKSE::log::warn("[FeedSession] PlayIdle failed, would retry... (disabled)");

                        // Retry logic placeholder - currently just fail
                        // In future: schedule delayed retry via thread + AddTask
                    }

                    // For now, always fail on PlayIdle failure
                    g_State.store(State::Failed);
                    CleanupSession(fmt::format("PlayIdle failed on attempt {}", attemptNum));
                }
            };

            // Set waiting flag for KillMoveStart detection
            g_WaitingForKillMoveStart.store(true);
            g_PlayIdleTime = std::chrono::steady_clock::now();
            SKSE::log::debug("[FeedSession] Waiting for KillMoveStart event...");

            // Play the idle (AnimUtil::playIdle is generic - no feed-specific cleanup)
            AnimUtil::playIdle(player, idle, target, playIdleCallback, session.isPaired);
        }

        // Internal timeout handler
        void OnTimeout() {
            State current = g_State.load();
            if (current == State::Playing || current == State::Starting || current == State::Retrying) {
                SKSE::log::warn("[FeedSession] Timeout triggered in state {}", static_cast<int>(current));
                g_State.store(State::Failed);
                CleanupSession("Session timeout");
            }
        }

    }  // anonymous namespace

    //=========================================================================
    // PRIMARY API IMPLEMENTATION
    //=========================================================================

    bool Start(
        RE::Actor* player,
        RE::Actor* target,
        RE::TESIdleForm* idle,
        bool isPaired,
        int feedType,
        OnAnimationStarted onStarted,
        OnSessionComplete onComplete)
    {
        // Check if already active
        State expected = State::Idle;
        if (!g_State.compare_exchange_strong(expected, State::Starting)) {
            SKSE::log::warn("[FeedSession] Start called but session already active (state={})",
                static_cast<int>(g_State.load()));
            return false;
        }

        // Validate inputs
        if (!player || !idle) {
            SKSE::log::error("[FeedSession] Invalid inputs: player={}, idle={}",
                player ? "valid" : "null", idle ? "valid" : "null");
            g_State.store(State::Idle);
            return false;
        }

        if (isPaired && !target) {
            SKSE::log::error("[FeedSession] Paired animation requires target");
            g_State.store(State::Idle);
            return false;
        }

        SKSE::log::info("========== FEED STARTED ==========");
        SKSE::log::info("[FeedSession] Starting: isPaired={}, feedType={}", isPaired, feedType);

        // Initialize session data
        {
            std::lock_guard<std::mutex> lock(g_SessionMutex);

            g_Session = SessionData{};
            g_Session.playerHandle = player->GetHandle();
            g_Session.targetHandle = target ? target->GetHandle() : RE::ObjectRefHandle{};
            g_Session.idleFormID = idle->GetFormID();
            g_Session.isPaired = isPaired;
            g_Session.feedType = feedType;
            g_Session.startTime = std::chrono::steady_clock::now();
            g_Session.attemptNumber = 0;

            g_SetupFlags = SetupFlags{};
            g_OnStarted = std::move(onStarted);
            g_OnComplete = std::move(onComplete);
        }

        // Perform setup on main thread
        auto playerHandle = player->GetHandle();
        auto targetHandle = target ? target->GetHandle() : RE::ObjectRefHandle{};
        bool playerInCombat = player->IsInCombat();
        bool targetInCombat = target && target->IsInCombat();

        SKSE::GetTaskInterface()->AddTask([playerHandle, targetHandle, isPaired, feedType, playerInCombat, targetInCombat]() {
            // Resolve handles
            auto playerRef = playerHandle.get();
            if (!playerRef) {
                SKSE::log::error("[FeedSession] Player handle invalid during setup");
                g_State.store(State::Failed);
                CleanupSession("Player handle invalid during setup");
                return;
            }

            auto* player = playerRef->As<RE::Actor>();
            RE::Actor* target = nullptr;

            if (targetHandle) {
                auto targetRef = targetHandle.get();
                if (targetRef) {
                    target = targetRef->As<RE::Actor>();
                }
            }

            if (!player) {
                SKSE::log::error("[FeedSession] Player cast failed during setup");
                g_State.store(State::Failed);
                CleanupSession("Player cast failed during setup");
                return;
            }

            // === SETUP PHASE (track everything we do) ===
            {
                std::lock_guard<std::mutex> lock(g_SessionMutex);

                // Set kill move flags
                AnimUtil::SetInKillMove(player, true);
                g_SetupFlags.playerKillMoveSet = true;
                SKSE::log::debug("[FeedSession] Set player kill move flag");

                if (target) {
                    AnimUtil::SetInKillMove(target, true);
                    g_SetupFlags.targetKillMoveSet = true;
                    SKSE::log::debug("[FeedSession] Set target kill move flag");
                }

                // Pacify if in combat
                if (target && (targetInCombat || playerInCombat)) {
                    AnimUtil::PacifyActor(target);
                    g_SetupFlags.targetPacified = true;
                    SKSE::log::debug("[FeedSession] Pacified target");
                }

                // Set graph variables
                AnimUtil::SetFeedGraphVars(player, feedType);
                if (target) {
                    AnimUtil::SetFeedGraphVars(target, feedType);
                }
                g_SetupFlags.graphVarsSet = true;
                SKSE::log::debug("[FeedSession] Set feed graph vars (feedType={})", feedType);

                // Register animation event sink
                AnimEventSink::Register();
                g_SetupFlags.eventSinkRegistered = true;
                SKSE::log::debug("[FeedSession] Registered animation event sink");

                // Apply time slowdown if enabled and in combat
                auto* settings = Settings::GetSingleton();
                if (settings->Animation.EnableTimeSlowdown && playerInCombat) {
                    if (auto* timer = RE::BSTimer::GetSingleton()) {
                        timer->SetGlobalTimeMultiplier(settings->Animation.TimeSlowdownMultiplier, true);
                        g_SetupFlags.timeSlowdownApplied = true;
                        SKSE::log::info("[FeedSession] Time slowdown applied: {}x",
                            settings->Animation.TimeSlowdownMultiplier);
                    }
                }
            }

            // Attempt to play the animation
            AttemptPlayIdle();
        });

        return true;
    }

    bool IsActive() {
        State s = g_State.load();
        return s == State::Starting || s == State::Playing || s == State::Retrying;
    }

    //=========================================================================
    // ANIMATION EVENT HANDLERS
    //=========================================================================

    void OnAnimationEnded() {
        State current = g_State.load();

        // Only process if we're in a state that expects animation end
        if (current == State::Playing) {
            SKSE::log::info("[FeedSession] Animation ended normally");
            g_State.store(State::Ending);
            CleanupSession("Animation completed");
        } else if (current == State::Starting || current == State::Retrying) {
            // Animation ended before we expected - might be interrupted
            SKSE::log::warn("[FeedSession] Animation ended unexpectedly in state {}",
                static_cast<int>(current));
            g_State.store(State::Failed);
            CleanupSession("Animation ended unexpectedly");
        }
        // If already Idle/Ending/Failed, ignore
    }

    void CheckTimeout() {
        State current = g_State.load();
        if (current != State::Playing && current != State::Starting && current != State::Retrying) {
            return;
        }

        std::chrono::steady_clock::time_point startTime;
        {
            std::lock_guard<std::mutex> lock(g_SessionMutex);
            startTime = g_Session.startTime;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

        float timeout = Settings::GetSingleton()->General.AnimationTimeout;

        if (elapsed >= static_cast<long long>(timeout)) {
            SKSE::GetTaskInterface()->AddTask([]() {
                OnTimeout();
            });
        }
    }

    //=========================================================================
    // STATE QUERIES
    //=========================================================================

    RE::NiPointer<RE::Actor> GetActiveFeedTarget() {
        std::lock_guard<std::mutex> lock(g_SessionMutex);

        State current = g_State.load();
        if (current == State::Idle || current == State::Failed) {
            return nullptr;
        }

        auto ref = g_Session.targetHandle.get();
        if (!ref) {
            return nullptr;
        }

        return RE::NiPointer<RE::Actor>(ref->As<RE::Actor>());
    }

    bool CheckAndClearFeedEnded() {
        bool expected = true;
        return g_FeedEndedFlag.compare_exchange_strong(expected, false);
    }

    bool CheckKillMoveStartDetected() {
        bool expected = true;
        if (g_WaitingForKillMoveStart.compare_exchange_strong(expected, false)) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - g_PlayIdleTime).count();
            SKSE::log::info("[FeedSession] KillMoveStart detected after {}ms", elapsed);
            return true;
        }
        return false;
    }

}  // namespace FeedSession
