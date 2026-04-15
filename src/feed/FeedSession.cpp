#include "PCH.h"
#include "feed/FeedSession.h"
#include "feed/PairedAnimPromptSink.h"
#include "feed/CustomFeed.h"
#include "utils/AnimUtil.h"
#include "Settings.h"
#include <thread>

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
        constexpr int KILLMOVE_START_TIMEOUT_MS = 10;  // Time to wait for KillMoveStart before retry

        // Weapon sheathe waiting - polls weapon state until sheathed
        std::atomic<bool> g_WaitingForWeaponSheathe{false};
        std::chrono::steady_clock::time_point g_SheatheStartTime{};
        constexpr int WEAPON_SHEATHE_TIMEOUT_MS = 1000;  // 1 second timeout for sheathe
        constexpr int WEAPON_SHEATHE_POLL_MS = 50;       // Poll every 50ms

        // Track if we sheathed weapon so we can redraw after animation
        std::atomic<bool> g_WeaponWasSheathedForAnimation{false};

        // Configuration - retry enabled (10 attempts * 50ms = 500ms max)
        RetryConfig g_RetryConfig{
            .maxAttempts = 10,
            .retryDelayMs = 50,
            .retryOnPlayIdleFalse = true
        };
    }

    //=========================================================================
    // INTERNAL HELPERS
    //=========================================================================

    namespace {

        // Forward declarations
        void AttemptPlayIdle();
        void CleanupSession(const std::string& reason);
        void OnTimeout();
        void CheckKillMoveStartTimeout(int attemptNum);
        void PollWeaponSheatheState();

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
            bool shouldRedrawWeapon = g_WeaponWasSheathedForAnimation.exchange(false);

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

                // Redraw weapon if we sheathed it for the animation
                if (shouldRedrawWeapon) {
                    if (auto ref = playerHandle.get()) {
                        if (auto* player = ref->As<RE::Actor>()) {
                            player->DrawWeaponMagicHands(true);
                            SKSE::log::info("[FeedSession] Redrawing weapon after animation");
                        }
                    }
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

            // Clear sheathe waiting flag if still set
            g_WaitingForWeaponSheathe.store(false);

            // Set ended flag for prompt refresh logic
            g_FeedEndedFlag.store(true);
            g_State.store(State::Idle);
        }

        // Attempt to play the idle animation
        void AttemptPlayIdle() {
            SessionData session;

            {
                std::lock_guard<std::mutex> lock(g_SessionMutex);
                session = g_Session;
                session.attemptNumber++;
                g_Session.attemptNumber = session.attemptNumber;
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

            // Get onStarted callback for solo animations (paired uses CheckKillMoveStartDetected)
            OnAnimationStarted onStarted;
            {
                std::lock_guard<std::mutex> lock(g_SessionMutex);
                onStarted = g_OnStarted;
            }

            // Create PlayIdle callback
            // For PAIRED: onStarted callback is invoked in CheckKillMoveStartDetected() when KillMoveStart fires
            // For SOLO: onStarted callback is invoked here immediately (no KillMoveStart event for solo)
            bool isPaired = session.isPaired;
            auto playIdleCallback = [attemptNum = session.attemptNumber, isPaired, onStarted](bool success, RE::Actor* callbackTarget) {
                if (success) {
                    g_State.store(State::Playing);
                    SKSE::log::info("[FeedSession] PlayIdle returned true on attempt {}", attemptNum);

                    // For solo animations, invoke callback immediately (no KillMoveStart event)
                    if (!isPaired && onStarted) {
                        SKSE::log::info("[FeedSession] Solo animation - running integration callback immediately");
                        onStarted(callbackTarget);
                    }
                    // For paired, integration callback will be invoked when KillMoveStart is detected
                } else {
                    // PlayIdle returned false - immediate failure
                    g_State.store(State::Failed);
                    CleanupSession(fmt::format("PlayIdle returned false on attempt {}", attemptNum));
                }
            };

            // Only do KillMoveStart detection for paired animations
            if (session.isPaired) {
                g_WaitingForKillMoveStart.store(true);
                g_PlayIdleTime = std::chrono::steady_clock::now();
                SKSE::log::debug("[FeedSession] Waiting for KillMoveStart event (paired animation)...");
            } else {
                SKSE::log::debug("[FeedSession] Solo animation - skipping KillMoveStart detection");
            }

            // Play the idle (AnimUtil::playIdle is generic - no feed-specific cleanup)
            AnimUtil::playIdle(player, idle, target, playIdleCallback, session.isPaired);

            // Schedule KillMoveStart timeout check - ONLY for paired animations
            if (session.isPaired) {
                int currentAttempt = session.attemptNumber;
                std::thread([currentAttempt]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(KILLMOVE_START_TIMEOUT_MS));
                    SKSE::GetTaskInterface()->AddTask([currentAttempt]() {
                        CheckKillMoveStartTimeout(currentAttempt);
                    });
                }).detach();
            }
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

        // KillMoveStart timeout check - called after delay to detect failed animation start
        void CheckKillMoveStartTimeout(int attemptNum) {
            // Only act if still waiting for KillMoveStart
            if (!g_WaitingForKillMoveStart.load()) {
                return;  // Already detected or session ended
            }

            // Check if we're still in a state that expects animation
            State current = g_State.load();
            if (current != State::Playing) {
                return;  // Session already failed/ended
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - g_PlayIdleTime).count();

            SKSE::log::warn("[FeedSession] KillMoveStart timeout after {}ms - animation failed to start", elapsed);

            // Clear waiting flag
            g_WaitingForKillMoveStart.store(false);

            // Check retry config
            if (g_RetryConfig.retryOnPlayIdleFalse && attemptNum < g_RetryConfig.maxAttempts) {
                SKSE::log::info("[FeedSession] Retrying PlayIdle (attempt {}/{})",
                    attemptNum + 1, g_RetryConfig.maxAttempts);
                AttemptPlayIdle();  // Retry
            } else {
                SKSE::log::error("[FeedSession] Max retries reached or retry disabled - failing session");
                g_State.store(State::Failed);
                CleanupSession("KillMoveStart timeout - animation failed to start");
            }
        }

        // Poll weapon state until sheathed, then proceed with animation
        void PollWeaponSheatheState() {
            if (!g_WaitingForWeaponSheathe.load()) {
                return;  // No longer waiting
            }

            // Check timeout
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - g_SheatheStartTime).count();

            if (elapsed >= WEAPON_SHEATHE_TIMEOUT_MS) {
                SKSE::log::warn("[FeedSession] Sheathe timeout ({}ms) - proceeding anyway", elapsed);
                g_WaitingForWeaponSheathe.store(false);
                AttemptPlayIdle();
                return;
            }

            // Check if weapon is now sheathed
            SessionData session;
            {
                std::lock_guard<std::mutex> lock(g_SessionMutex);
                session = g_Session;
            }

            auto playerRef = session.playerHandle.get();
            if (!playerRef) {
                SKSE::log::error("[FeedSession] Player handle invalid during sheathe poll");
                g_WaitingForWeaponSheathe.store(false);
                g_State.store(State::Failed);
                CleanupSession("Player handle invalid during sheathe poll");
                return;
            }

            auto* player = playerRef->As<RE::Actor>();
            if (!player) {
                SKSE::log::error("[FeedSession] Player cast failed during sheathe poll");
                g_WaitingForWeaponSheathe.store(false);
                g_State.store(State::Failed);
                CleanupSession("Player cast failed during sheathe poll");
                return;
            }

            auto* playerState = player->AsActorState();
            auto weaponState = playerState ? playerState->GetWeaponState() : RE::WEAPON_STATE::kSheathed;

            if (weaponState == RE::WEAPON_STATE::kSheathed) {
                SKSE::log::info("[FeedSession] Weapon sheathed after {}ms - proceeding with animation", elapsed);
                g_WaitingForWeaponSheathe.store(false);
                AttemptPlayIdle();
                return;
            }

            // Still sheathing - poll again
            SKSE::log::debug("[FeedSession] Weapon state: {} - polling again...", static_cast<int>(weaponState));
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(WEAPON_SHEATHE_POLL_MS));
                SKSE::GetTaskInterface()->AddTask([]() {
                    PollWeaponSheatheState();
                });
            }).detach();
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

                // Set kill move flags - ONLY for paired animations
                // Solo animations don't need kill move flags (player only, no sync needed)
                if (isPaired) {
                    AnimUtil::SetInKillMove(player, true);
                    g_SetupFlags.playerKillMoveSet = true;
                    SKSE::log::debug("[FeedSession] Set player kill move flag");

                    if (target) {
                        AnimUtil::SetInKillMove(target, true);
                        g_SetupFlags.targetKillMoveSet = true;
                        SKSE::log::debug("[FeedSession] Set target kill move flag");
                    }

                    // Pacify if in combat - ONLY for paired animations
                    if (target && (targetInCombat || playerInCombat)) {
                        AnimUtil::PacifyActor(target);
                        g_SetupFlags.targetPacified = true;
                        SKSE::log::debug("[FeedSession] Pacified target");
                    }
                } else {
                    SKSE::log::debug("[FeedSession] Solo animation - skipping kill move flags and pacify");
                }

                // Set graph variables
                AnimUtil::SetFeedGraphVars(player, feedType);
                if (target) {
                    AnimUtil::SetFeedGraphVars(target, feedType);
                }
                g_SetupFlags.graphVarsSet = true;
            

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

            // For solo animations, sheathe weapon first if drawn
            // Paired animations handle weapon state differently
            if (!isPaired) {
                auto* playerState = player->AsActorState();
                if (playerState && playerState->IsWeaponDrawn()) {
                    SKSE::log::info("[FeedSession] Solo animation - weapon drawn, sheathing with native function");

                    g_WaitingForWeaponSheathe.store(true);
                    g_WeaponWasSheathedForAnimation.store(true);  // Track so we redraw after animation
                    g_SheatheStartTime = std::chrono::steady_clock::now();

                    // Call native sheathe function
                    player->DrawWeaponMagicHands(false);

                    // Start polling for weapon state to become sheathed
                    std::thread([]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(WEAPON_SHEATHE_POLL_MS));
                        SKSE::GetTaskInterface()->AddTask([]() {
                            PollWeaponSheatheState();
                        });
                    }).detach();
                    return;
                }
            }

            // Proceed with animation
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

            // NOW invoke the integration callback - animation actually started
            OnAnimationStarted onStarted;
            RE::Actor* target = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_SessionMutex);
                onStarted = g_OnStarted;
                if (auto ref = g_Session.targetHandle.get()) {
                    target = ref->As<RE::Actor>();
                }
            }
            if (onStarted) {
                SKSE::log::info("[FeedSession] Running integration callback");
                onStarted(target);
            }

            return true;
        }
        return false;
    }

}  // namespace FeedSession
