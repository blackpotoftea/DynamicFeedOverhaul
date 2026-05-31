#include "PCH.h"
#include "feed/PairedAnimPromptSink.h"
#include "Settings.h"
#include "feed/TargetState.h"
#include "papyrus/PapyrusCall.h"
#include "feed/CustomFeed.h"
#include "feed/FeedFiltering.h"
#include "feed/TwoSingleFeed.h"
#include "feed/FeedIconOverlay.h"
#include "integration/OStimIntegration.h"
#include "integration/VampireIntegrationUtils.h"
#include "integration/VampireFeedProxyIntegration.h"
#include "utils/MenuCheck.h"
#include "feed/AnimationRegistry.h"
#include "utils/AnimUtil.h"
#include "feed/WitnessDetection.h"
#include <thread>
#include <algorithm>

extern std::atomic<SkyPromptAPI::ClientID> g_clientID;

namespace FeedAnimState {
    // Single atomic state enum prevents race conditions between coupled states
    enum class State {
        Idle,     // No feed active
        Active,   // Feed in progress
        Ended     // Feed just ended (needs acknowledgment)
    };

    std::atomic<State> feedState{State::Idle};

    void MarkFeedStarted() {
        feedState.store(State::Active, std::memory_order_release);
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
        feedState.store(State::Ended, std::memory_order_release);
        SKSE::log::info("========== FEED ENDED ==========");

        // Always reset time multiplier to normal (safe even if not slowed)
        auto* timer = RE::BSTimer::GetSingleton();
        if (timer) {
            timer->SetGlobalTimeMultiplier(1.0f, true);
        }

        // Clear kill move flag (was set to prevent Quick Loot etc. during animation)
        // Get target before clearing the reference
        auto target = PairedAnimPromptSink::GetSingleton()->GetActiveFeedTarget();
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            AnimUtil::SetInKillMove(player, false);
        }
        if (target) {
            AnimUtil::SetInKillMove(target.get(), false);
        }

        // Clear the active feed target (thread-safe)
        PairedAnimPromptSink::GetSingleton()->SetActiveFeedTarget(nullptr);

        CustomFeed::OnComplete();
        // disable as require more refactor
        //TwoSingleFeed::OnComplete();
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
}

// AnimEventSink Implementation
std::chrono::steady_clock::time_point AnimEventSink::registeredTime_{};
std::mutex AnimEventSink::mutex_;

AnimEventSink* AnimEventSink::GetSingleton() {
    static AnimEventSink singleton;
    return &singleton;
}

RE::BSEventNotifyControl AnimEventSink::ProcessEvent(
    const RE::BSAnimationGraphEvent* event,
    [[maybe_unused]] RE::BSTEventSource<RE::BSAnimationGraphEvent>* source)
{
    if (!event || !event->tag.data()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto& tag = event->tag;

    if (tag == "PairEnd" || tag == "IdleStop") {
        SKSE::log::info("{} detected - marking feed ended", tag.c_str());

        {
            std::lock_guard<std::mutex> lock(mutex_);
            registeredTime_ = std::chrono::steady_clock::time_point{};
        }

        // Move to main thread to avoid race conditions on feedTargetHandle_
        SKSE::GetTaskInterface()->AddTask([]() {
            FeedAnimState::MarkFeedEnded();
            AnimEventSink::Unregister();
            SKSE::log::debug("Animation event sink unregistered (deferred)");
        });
    } else {
         // Log all events during feed to discover weapon-related events
        //  SKSE::log::debug("[AnimEvent] {}", tag.c_str());
    }

    return RE::BSEventNotifyControl::kContinue;
}

void AnimEventSink::Register() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Remove first to prevent double registration
        player->RemoveAnimationGraphEventSink(GetSingleton());
        player->AddAnimationGraphEventSink(GetSingleton());
        registeredTime_ = std::chrono::steady_clock::now();
        SKSE::log::info("Animation event sink registered");
    }
}

void AnimEventSink::Unregister() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        // Safe to call even if not registered (idempotent)
        player->RemoveAnimationGraphEventSink(GetSingleton());
        SKSE::log::debug("Animation event sink unregistered");
    }
}

void AnimEventSink::CheckTimeout() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (registeredTime_.time_since_epoch().count() == 0) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - registeredTime_).count();
    
    float timeout = Settings::GetSingleton()->General.AnimationTimeout;

    if (elapsed >= timeout) {
        SKSE::log::warn("Animation event timeout ({:.1f}s) - forcing prompt refresh", timeout);
        registeredTime_ = std::chrono::steady_clock::time_point{}; // Reset
        lock.unlock(); // Release lock before calling external functions that might call back or take time

        // Move to main thread to avoid race conditions on feedTargetHandle_
        SKSE::GetTaskInterface()->AddTask([]() {
            FeedAnimState::MarkFeedEnded();
            AnimEventSink::Unregister();
        });
    }
}

// PairedAnimPromptSink Implementation

PairedAnimPromptSink* PairedAnimPromptSink::GetSingleton() {
    static PairedAnimPromptSink singleton;
    return &singleton;
}

PairedAnimPromptSink::PairedAnimPromptSink() {
    UpdateFeedButtons();
    RegisterCorePromptCallback();
}

void PairedAnimPromptSink::RegisterCorePromptCallback() {
    RegisterPromptCallback([](RE::Actor* target) -> std::vector<PromptDef> {
        std::vector<PromptDef> prompts;
        if (!target) return prompts;

        auto* settings = Settings::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();

        bool isDead = target->IsDead();
        bool targetInCombat = target->IsInCombat();
        bool playerInCombat = player && player->IsInCombat();
        bool isEssential = TargetState::IsEssentialOrProtected(target);

        if (isDead) {
            bool isWerewolf = player && TargetState::IsWerewolf(player);

            if (isWerewolf) {
                // Werewolf - devour corpse
                prompts.push_back({
                    .text = "Devour",
                    .type = SkyPromptAPI::PromptType::kSinglePress,
                    .color = 0xFF8844FF,  // Orange-red for savage feeding
                    .priority = 1000,
                    .onAccept = nullptr
                });
            } else {
                // Vampire - drain corpse
                prompts.push_back({
                    .text = "Drain Corpse",
                    .type = SkyPromptAPI::PromptType::kSinglePress,
                    .color = 0xFFFFFFFF,
                    .priority = 1000,
                    .onAccept = nullptr
                });
            }
        }
        else if (playerInCombat || targetInCombat) {
            // Combat - red color, auto-lethal
            prompts.push_back({
                .text = "Kill Feed",
                .type = SkyPromptAPI::PromptType::kSinglePress,
                .color = 0xFF5555FF,  // Red
                .priority = 1000,
                .onAccept = [](RE::Actor*, bool) {
                    // Combat feed is always lethal
                    PairedAnimPromptSink::GetSingleton()->isLethalFeedInProgress_ = true;
                }
            });
        }
        else {
            // Non-combat
            bool canLethal = settings->NonCombat.EnableLethalFeed &&
                            !(settings->NonCombat.ExcludeEssentialFromLethal && isEssential);

            if (canLethal) {
                prompts.push_back({
                    .text = "Feed (Hold to Kill)",
                    .type = SkyPromptAPI::PromptType::kHold,
                    .holdDuration = settings->NonCombat.LethalHoldDuration,
                    .color = 0xFF5555FF,  // Red warning
                    .priority = 1000,
                    .onAccept = [](RE::Actor*, bool holdComplete) {
                        // Only set state - ProcessEvent calls HandleFeedAccepted
                        PairedAnimPromptSink::GetSingleton()->isLethalFeedInProgress_ = holdComplete;
                    }
                });
            } else {
                prompts.push_back({
                    .text = "Feed",
                    .type = SkyPromptAPI::PromptType::kSinglePress,
                    .color = 0xFFFFFFFF,
                    .priority = 1000,
                    .onAccept = nullptr  // No state to set
                });
            }
        }

        return prompts;
    });
}

void PairedAnimPromptSink::UpdateFeedButtons() {
    auto* settings = Settings::GetSingleton();

    // Primary button bindings
    feedButtons_ = {{
        {RE::INPUT_DEVICE::kKeyboard, static_cast<SkyPromptAPI::ButtonID>(settings->Input.FeedKey)},
        {RE::INPUT_DEVICE::kGamepad, static_cast<SkyPromptAPI::ButtonID>(settings->Input.FeedGamepadKey)}
    }};

    // Secondary button bindings
    secondaryButtons_ = {{
        {RE::INPUT_DEVICE::kKeyboard, static_cast<SkyPromptAPI::ButtonID>(settings->Input.SecondaryKey)},
        {RE::INPUT_DEVICE::kGamepad, static_cast<SkyPromptAPI::ButtonID>(settings->Input.SecondaryGamepadKey)}
    }};

    // If there's an active target, refresh the prompt with new buttons
    auto currentTargetPtr = GetTarget();
    if (currentTargetPtr) {
        SKSE::log::debug("UpdateFeedButtons: Refreshing prompt for current target");
        ShowPrompt(currentTargetPtr.get());
    }
}

void PairedAnimPromptSink::RegisterPromptCallback(PromptCallback callback) {
    promptCallbacks_.push_back(std::move(callback));
    SKSE::log::info("Registered prompt callback (total: {})", promptCallbacks_.size());
}

std::span<const SkyPromptAPI::Prompt> PairedAnimPromptSink::GetPrompts() const {
    return prompts_;
}

void PairedAnimPromptSink::ProcessEvent(SkyPromptAPI::PromptEvent event) const {
    SKSE::log::info("ProcessEvent - eventType: {}, promptType: {}, actionID: {}, text: '{}'",
        static_cast<int>(event.type),
        static_cast<int>(event.prompt.type),
        event.prompt.actionID,
        event.prompt.text);

    // Get non-const singleton - ProcessEvent is const due to API contract,
    // but we need to modify state. Using singleton access is the proper pattern here.
    auto* self = GetSingleton();

    // Handle timing out separately (not tied to specific prompt)
    if (event.type == SkyPromptAPI::PromptEventType::kTimingOut) {
        self->HandleTimingOut();
        return;
    }

    // Find matching PromptDef by actionID (actionID = index in currentPromptDefs_)
    const PromptDef* matchedDef = nullptr;
    size_t actionIndex = static_cast<size_t>(event.prompt.actionID);
    if (actionIndex < self->currentPromptDefs_.size()) {
        matchedDef = &self->currentPromptDefs_[actionIndex];
    }

    if (!matchedDef) {
        SKSE::log::warn("ProcessEvent: No matching PromptDef found");
        return;
    }

    auto targetPtr = self->GetTarget();
    RE::Actor* target = targetPtr ? targetPtr.get() : nullptr;

    switch (event.type) {
    case SkyPromptAPI::PromptEventType::kDown:
        SKSE::log::debug("kDown event - button pressed");
        break;

    case SkyPromptAPI::PromptEventType::kUp:
        // Button released early - for kHold this is non-lethal feed
        if (event.prompt.type == SkyPromptAPI::PromptType::kHold) {
            SKSE::log::info("kUp on kHold - early release, executing non-lethal feed");
            if (matchedDef->onAccept) matchedDef->onAccept(target, false);
            self->HandleFeedAccepted();
        }
        break;

    case SkyPromptAPI::PromptEventType::kAccepted:
        {
            bool holdComplete = (event.prompt.type == SkyPromptAPI::PromptType::kHold);
            SKSE::log::info("kAccepted - executing (holdComplete={})", holdComplete);
            if (matchedDef->onAccept) matchedDef->onAccept(target, holdComplete);
            self->HandleFeedAccepted();
        }
        break;

    default:
        SKSE::log::debug("Unhandled event type: {}", static_cast<int>(event.type));
        break;
    }
}




void PairedAnimPromptSink::ExecuteFeed(const char* idleEditorID, RE::Actor* target, bool isPairedAnim, bool isLethal, bool hasOARAnimation) {
    auto* settings = Settings::GetSingleton();

    // if (settings->NonCombat.UseTwoSingleAnimations && isPairedAnim) {
    //     SKSE::log::info("Using two-single animation mode");
    //     // temporary disabled
    //     // if (TwoSingleFeed::PlayTwoSingleFeed(target)) {
    //     //     PapyrusCall::SendOnVampireFeedEvent(target);
    //     //     auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
    //     //     if (vampireQuest) {
    //     //         PapyrusCall::CallVampireFeed(vampireQuest, target);
    //     //     } else {
    //     //         SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
    //     //     }
    //     //     return;
    //     // }
    //     SKSE::log::warn("Two-single feed failed, falling back to paired animation");
    // }


    SKSE::log::info("Playing feed idle '{}' (paired={}, lethal={})", idleEditorID, isPairedAnim, isLethal);

    // Create callback that runs AFTER animation starts successfully (on game thread)
    // This ensures integration logic runs only when animation is actually playing
    auto onAnimationResult = [isLethal, hasOARAnimation](bool success, RE::Actor* callbackTarget) {
        if (!success) {
            SKSE::log::warn("CustomFeed failed - animation did not start");
            return;
        }

        SKSE::log::info("Animation started successfully - running integration");

        auto* player = RE::PlayerCharacter::GetSingleton();

        // Check if VampireFeedProxy handles vampire feed - if so, skip vanilla feed calls
        auto* settings = Settings::GetSingleton();
        bool proxyHandlesFeed = settings->Integration.EnableVampireFeedProxy &&
                                VampireFeedProxyIntegration::IsAvailable();

        if (proxyHandlesFeed) {
            SKSE::log::info("VampireFeedProxy detected - skipping vanilla vampire feed event");
        } else {
            PapyrusCall::SendOnVampireFeedEvent(callbackTarget);
        }

        // Send custom DAO_VampireFeed event with attacker and target (always send our custom event)
        if (player) {
            PapyrusCall::SendDAO_VampireFeedEvent(player, callbackTarget);
        }

        // Only call vampire script if NOT a werewolf AND proxy is not handling it
        if (player && !TargetState::IsWerewolf(player) && !proxyHandlesFeed) {
            auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
            if (vampireQuest) {
                // If lethal, the kill move animation handles the kill - don't double-kill in integration
                bool animationHandlesKill = isLethal;
                PapyrusCall::CallVampireFeed(vampireQuest, callbackTarget, isLethal, animationHandlesKill);
            } else {
                SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
            }
        }
        // Werewolf corpse feeding
        else if (player && TargetState::IsWerewolf(player) && callbackTarget && callbackTarget->IsDead()) {
            SKSE::log::info("Werewolf corpse feed - applying effects");

            // 1. Apply PlayerWerewolfFeedVictimSpell to player
            auto* feedSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("PlayerWerewolfFeedVictimSpell");
            if (feedSpell) {
                VampireIntegrationUtils::CastSpell(feedSpell, player, player);
                SKSE::log::debug("Applied PlayerWerewolfFeedVictimSpell");
            } else {
                SKSE::log::warn("PlayerWerewolfFeedVictimSpell not found");
            }

            // 2. Call PlayerWerewolfChangeScript.Feed()
            auto* werewolfQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("PlayerWerewolfQuest");
            if (werewolfQuest) {
                VampireIntegrationUtils::CallPapyrusMethod(werewolfQuest, "PlayerWerewolfChangeScript", "Feed");
                SKSE::log::debug("Called PlayerWerewolfChangeScript.Feed()");
            } else {
                SKSE::log::warn("PlayerWerewolfQuest not found");
            }
        }

        // Integration-specific post-feed handling
        PapyrusCall::VampireIntegration integration = PapyrusCall::DetectVampireIntegration();
        switch (integration) {
            case PapyrusCall::VampireIntegration::Sacrosanct:
                SKSE::log::debug("Post-feed: Sacrosanct integration active - letting Sacrosanct handle kill");
                // Sacrosanct handles killing via ProcessFeed call above
                // No additional action needed
                break;

            case PapyrusCall::VampireIntegration::BetterVampires:
                SKSE::log::debug("Post-feed: Better Vampires integration active");
                // Better Vampires may handle killing differently
                // No additional action needed for now
                break;

            case PapyrusCall::VampireIntegration::Vanilla:
            default:
                SKSE::log::debug("Post-feed: Vanilla vampire system active");
                // Only kill if:
                // 1. User wants lethal feed
                // 2. NO OAR combat animation found (if OAR anim exists, kill is baked in)
                if (isLethal && callbackTarget && !hasOARAnimation) {
                    SKSE::log::info("No OAR animation found - manually killing target after animation");
                    AnimUtil::KillTarget(callbackTarget);
                } else if (isLethal && hasOARAnimation) {
                    SKSE::log::info("OAR combat animation found - letting animation handle kill");
                }
                break;
        }
    };

    // PlayPairedFeed now takes callback - integration runs after animation starts
    CustomFeed::PlayPairedFeed(idleEditorID, target, isPairedAnim, onAnimationResult);
}

// We have 2 animation systems Vannila Idle and OAR which we set via GraphVariable
// We need both select idle -> set correct graph variable to match OAR animations
void PairedAnimPromptSink::HandleFeedAccepted() {
    auto feedTargetPtr = GetTarget();
    if (!feedTargetPtr) return;

    // Safe to use raw pointer now - NiPointer keeps it alive for entire function scope
    RE::Actor* feedTarget = feedTargetPtr.get();

    auto* settings = Settings::GetSingleton();

    // Store the feed target for witness detection (thread-safe)
    SetActiveFeedTarget(feedTarget);

    HidePrompt();

    FeedAnimState::MarkFeedStarted();
    AnimEventSink::Register();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    
    // Mark player and target as in kill move to prevent Quick Loot and other mods from interfering
    AnimUtil::SetInKillMove(player, true);
    AnimUtil::SetInKillMove(feedTarget, true);

    auto furnitureRef = TargetState::GetFurnitureReference(feedTarget);

    SKSE::log::info("Feed ACCEPTED on target: {} (FormID: {:X})",
        feedTarget->GetName(), feedTarget->GetFormID());

    if (settings->IconOverlay.EnableIconOverlay) {
        // Trigger bite animation instead of just stopping
        FeedIconOverlay::GetSingleton()->TriggerFeedAnimation();
    }

    bool isInCombat = false;  // Target's combat state
    int targetState = AnimUtil::DetermineTargetState(feedTarget, isInCombat);

    // Player's combat state forces lethal feed
    bool playerInCombat = player->IsInCombat();
    SKSE::log::debug("Target state: {} (targetCombat={}, playerCombat={})", targetState, isInCombat, playerInCombat);

    // Pacify target during combat to prevent attack interruption
    if (isInCombat || playerInCombat) {
        AnimUtil::PacifyActor(feedTarget);
    }

    int vampireStage = PapyrusCall::GetVampireStage();
    bool useTwoSingle = settings->NonCombat.UseTwoSingleAnimations && targetState == Feed::kStanding;

    if (useTwoSingle) {
        // int feedType = 0;

        // AnimUtil::SetFeedGraphVars(player, feedType);
        // AnimUtil::SetFeedGraphVars(feedTarget, feedType);

        // ExecuteFeed(nullptr, feedTarget, true);
    } else {
        // Execute all positioning/rotation in a single main-thread task
        auto playerHandle = player->CreateRefHandle();
        auto targetHandle = feedTarget->CreateRefHandle();

        SKSE::GetTaskInterface()->AddTask([playerHandle, targetHandle, targetState, settings]() {
            auto playerRef = playerHandle.get();
            auto targetRef = targetHandle.get();
            if (!playerRef || !targetRef) {
                SKSE::log::warn("Position task: Handle resolution failed");
                return;
            }

            auto* player = playerRef.get()->As<RE::Actor>();
            auto* target = targetRef.get()->As<RE::Actor>();
            if (!player || !target) {
                SKSE::log::warn("Position task: Actor cast failed");
                return;
            }

            SKSE::log::debug("Position task: targetState={}, EnableHeightAdjust={}",
                targetState, settings->NonCombat.EnableHeightAdjust);

            if (targetState == Feed::kStanding && settings->NonCombat.EnableHeightAdjust) {
                auto playerPos = player->GetPosition();
                auto targetPos = target->GetPosition();
                float heightDiff = std::fabs(targetPos.z - playerPos.z);
                SKSE::log::info("Height check BEFORE adjustment: player Z={:.2f}, target Z={:.2f}, diff={:.2f}",
                    playerPos.z, targetPos.z, heightDiff);

                AnimUtil::ApplyHeightAdjustment(player, target, settings->NonCombat.MinHeightDiff, settings->NonCombat.MaxHeightDiff);

                // Log AFTER adjustment
                playerPos = player->GetPosition();
                targetPos = target->GetPosition();
                heightDiff = std::fabs(targetPos.z - playerPos.z);
                SKSE::log::info("Height check AFTER adjustment: player Z={:.2f}, target Z={:.2f}, diff={:.2f}",
                    playerPos.z, targetPos.z, heightDiff);
            }

            if (targetState == Feed::kStanding && settings->NonCombat.EnableRotation) {
                AnimUtil::RotateTargetToClosest(target, player);
                AnimUtil::RotateAttackerToTarget(player, target);
            }
        });

        // Calculate direction for animation selection (can be done immediately)
        bool isBehind = false;
        if (targetState == Feed::kStanding) {
            isBehind = AnimUtil::GetClosestDirection(feedTarget, player);
        } else {
            // For sitting/sleeping, just detect direction without rotating
            isBehind = AnimUtil::GetClosestDirection(feedTarget, player);
        }

        // --- New Registry Logic ---
        Feed::FeedContext context;
        context.player = player;
        context.target = feedTarget;
        context.isCombat = playerInCombat;  // Use PLAYER's combat state (forces lethal animations)
        context.isSneaking = player->IsSneaking();
        context.isHungry = (vampireStage >= settings->Animation.HungryThreshold);
        context.targetIsStanding = (targetState == Feed::kStanding);
        context.isBehind = isBehind;
        context.isLethal = isLethalFeedInProgress_ || playerInCombat;  // User choice OR forced by player combat

        const Feed::AnimationDefinition* anim = nullptr;

        if (settings->General.DebugAnimationCycle) {
            anim = Feed::AnimationRegistry::GetSingleton()->GetNextDebugAnimation(context);
        } else {
            anim = Feed::AnimationRegistry::GetSingleton()->GetBestMatch(context);
        }

        int feedType = 0;
        bool isLethal = false;
        std::string animName = "Default";

        if (anim) {
            feedType = anim->feedTypeID;
            isLethal = anim->isLethal;
            animName = anim->eventName;
        }

        // Override with user's choice if they held button for lethal feed
        if (isLethalFeedInProgress_) {
            isLethal = true;
            SKSE::log::info("Lethal feed triggered by hold duration");
        }

        // Force lethal when player is in combat
        if (playerInCombat) {
            isLethal = true;
            SKSE::log::info("Lethal feed forced - player in combat");
        }

        if (settings->General.ForceFeedType > 0){
            feedType = settings->General.ForceFeedType;
            SKSE::log::info("Animation override set ");
        }


        SKSE::log::info("Registry match: {} (Type: {}, Lethal: {})", animName, feedType, isLethal);

        // Check if we found a valid OAR animation (not "Default")
        bool hasOARAnimation = (anim != nullptr && animName != "Default");

        bool isPairedAnim = true;
        const char* idleEditorID = Feed::SelectIdleAnimation(targetState, feedTarget, furnitureRef, isBehind, isPairedAnim, isLethal);


        AnimUtil::SetFeedGraphVars(player, feedType);
        AnimUtil::SetFeedGraphVars(feedTarget, feedType);


        ExecuteFeed(idleEditorID, feedTarget, isPairedAnim, isLethal, hasOARAnimation);
        // Reset lethal flag after use (embrace flag reset by integration)
        isLethalFeedInProgress_ = false;
    }
}

void PairedAnimPromptSink::HandleTimingOut() {
    if (!GetTarget() || g_clientID.load(std::memory_order_acquire) == 0) return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player && AnimUtil::IsInPairedAnimation(player)) {
        SKSE::log::debug("Prompt timing out - skipped (in animation)");
        return;
    }

    auto targetPtr = GetTarget();
    if (targetPtr) {
        // Validate target before refreshing
        if (!IsValidFeedTarget(targetPtr.get())) {
            SKSE::log::debug("Prompt timing out - target invalid, removing prompt");
            HidePrompt();
            return;
        }

        ShowPrompt(targetPtr.get());
        SKSE::log::debug("Prompt timing out - refreshed");
    }
}

// Thread-safe wrapper methods for currentTargetHandle_
void PairedAnimPromptSink::SetTargetHandle(const RE::ObjectRefHandle& handle) {
    std::lock_guard<std::mutex> lock(targetMutex_);
    currentTargetHandle_ = handle;
}

RE::ObjectRefHandle PairedAnimPromptSink::GetTargetHandle() const {
    std::lock_guard<std::mutex> lock(targetMutex_);
    return currentTargetHandle_;
}

// Thread-safe wrapper methods for activeFeedTargetHandle_
void PairedAnimPromptSink::SetActiveFeedTarget(RE::Actor* target) {
    std::lock_guard<std::mutex> lock(targetMutex_);
    if (target) {
        activeFeedTargetHandle_ = target->GetHandle();
    } else {
        activeFeedTargetHandle_.reset();
    }
}

RE::NiPointer<RE::Actor> PairedAnimPromptSink::GetActiveFeedTarget() const {
    std::lock_guard<std::mutex> lock(targetMutex_);
    auto ref = activeFeedTargetHandle_.get();
    if (!ref) {
        return nullptr;
    }
    return RE::NiPointer<RE::Actor>(ref->As<RE::Actor>());
}

void PairedAnimPromptSink::SetTarget(RE::Actor* target) {
    // Store new target as handle
    if (target) {
        SetTargetHandle(target->GetHandle());
    } else {
        RE::ObjectRefHandle emptyHandle;
        SetTargetHandle(emptyHandle);
    }

    // Clear previous prompts
    currentPromptDefs_.clear();
    prompts_.clear();

    if (target) {
        // Collect prompts from all registered callbacks
        for (const auto& callback : promptCallbacks_) {
            auto defs = callback(target);
            for (auto& def : defs) {
                currentPromptDefs_.push_back(std::move(def));
            }
        }

        // Sort by priority (highest first)
        std::sort(currentPromptDefs_.begin(), currentPromptDefs_.end(),
            [](const PromptDef& a, const PromptDef& b) {
                return a.priority > b.priority;
            });

        SKSE::log::info("SetTarget: {} - collected {} prompts from callbacks",
            target->GetName(), currentPromptDefs_.size());

        // Convert to SkyPromptAPI::Prompt format
        // Limit to 2 prompts (primary and secondary buttons)
        size_t maxPrompts = std::min(currentPromptDefs_.size(), size_t(2));
        for (size_t i = 0; i < maxPrompts; ++i) {
            const auto& def = currentPromptDefs_[i];

            // Select button binding based on index
            auto& buttons = (i == 0) ? feedButtons_ : secondaryButtons_;

            prompts_.push_back(SkyPromptAPI::Prompt(
                def.text,
                static_cast<SkyPromptAPI::EventID>(i + 1),  // eventID - different row for each (1, 2, ...)
                static_cast<SkyPromptAPI::ActionID>(i),     // actionID - unique per prompt (0, 1, ...)
                def.type,
                target->GetFormID(),
                buttons,
                def.color,
                def.holdDuration
            ));

            SKSE::log::info("SetTarget: Prompt[{}] = '{}' (actionID={}, button=0x{:X})",
                i, def.text, i, static_cast<uint32_t>(buttons[0].second));
        }
    }
}

RE::NiPointer<RE::Actor> PairedAnimPromptSink::GetTarget() const {
    auto handle = GetTargetHandle();
    auto ref = handle.get();
    if (!ref) {
        return nullptr;
    }
    // NiPointer<Actor> keeps ref alive in the caller's scope
    return RE::NiPointer<RE::Actor>(ref->As<RE::Actor>());
}

bool PairedAnimPromptSink::IsExcluded(RE::Actor* actor) {
    if (!actor) {
        SKSE::log::debug("IsExcluded: actor is null");
        return true;
    }

    auto* settings = Settings::GetSingleton();
    if (!settings->General.EnableMod) {
        SKSE::log::debug("IsExcluded: mod disabled");
        return true;
    }

    if (FeedAnimState::IsFeedActive()) {
        SKSE::log::debug("IsExcluded: feed already active");
        return true;
    }
    
    // Check common filters first (fast)
    if (FeedFiltering::IsExcludedByFilters(actor)) return true;

    bool isInCombat = actor->IsInCombat();
    SKSE::log::debug("IsExcluded check: {} | InCombat: {}", actor->GetName(), isInCombat);

    if (isInCombat) {
        if (FeedFiltering::IsExcludedCombat(actor)) return true;
    } else {
        if (FeedFiltering::IsExcludedNonCombat(actor)) return true;
    }

    // Graph vars checks (slow)
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player && AnimUtil::IsInPairedAnimation(player)) {
        SKSE::log::debug("Excluded: player is in paired animation");
        return true;
    }
    if (AnimUtil::IsInPairedAnimation(actor)) {
        SKSE::log::debug("Excluded: {} is in paired animation", actor->GetName());
        return true;
    }
    
    return false;
}

bool PairedAnimPromptSink::IsValidFeedTarget(RE::Actor* target) {
    if (!target) {
        SKSE::log::debug("IsValidFeedTarget: false - no target");
        return false;
    }

    // 0. Check for Open Menus (New check)
    if (MenuCheck::IsAnyBlockedMenuOpen()) {
        SKSE::log::debug("IsValidFeedTarget: false - blocked menu open");
        return false;
    }

    // 0.5. Get player singleton (used throughout function)
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        SKSE::log::debug("IsValidFeedTarget: false - no player");
        return false;
    }

    // 0.6. Check prompt display conditions
    auto* settings = Settings::GetSingleton();

    // ShowWhenSneaking: If enabled and player is sneaking, always show prompt
    if (settings->PromptDisplay.ShowWhenSneaking && player->IsSneaking()) {
        SKSE::log::debug("IsValidFeedTarget: sneaking bypass - showing prompt");
        // Skip weapon/combat checks, continue to other validations
    }
    // RequireWeaponDrawn: If enabled, require weapon drawn OR in combat
    else if (settings->PromptDisplay.RequireWeaponDrawn) {
        auto* playerState = player->AsActorState();
        bool weaponDrawn = playerState && playerState->IsWeaponDrawn();
        bool playerInCombat = player->IsInCombat();

        if (!weaponDrawn && !playerInCombat) {
            SKSE::log::debug("IsValidFeedTarget: false - weapon/magic not drawn and not in combat");
            return false;
        }
    }

    // 0.7. Check if player is facing target (skip during combat if RelaxedCombatTargeting enabled)
    if (settings->PromptDisplay.RequirePlayerFacing) {
        bool skipFacingCheck = settings->PromptDisplay.RelaxedCombatTargeting && player->IsInCombat();
        if (!skipFacingCheck && !AnimUtil::IsPlayerFacingTarget(player, target, settings->PromptDisplay.FacingAngleThreshold)) {
            SKSE::log::debug("IsValidFeedTarget: false - player not facing target");
            return false;
        }
    }

    // 0.8. Check if player is swimming or riding
    if (AnimUtil::IsSwimming(player)) {
        SKSE::log::debug("IsValidFeedTarget: false - player is swimming");
        return false;
    }
    if (AnimUtil::IsRiding(player)) {
        SKSE::log::debug("IsValidFeedTarget: false - player is riding a mount");
        return false;
    }
    if (AnimUtil::IsJumping(player)) {
        SKSE::log::debug("IsValidFeedTarget: false - player is jumping");
        return false;
    }

    // 0.9. Check if target is swimming or riding
    if (AnimUtil::IsSwimming(target)) {
        SKSE::log::debug("IsValidFeedTarget: false - target {} is swimming", target->GetName());
        return false;
    }
    if (AnimUtil::IsRiding(target)) {
        SKSE::log::debug("IsValidFeedTarget: false - target {} is riding a mount", target->GetName());
        return false;
    }

    // 1. Check Player Status (Vampire/Werewolf, Hunger, Settings)
    // Pass target's combat state because it might bypass hunger checks
    bool targetInCombat = target->IsInCombat();
    if (!AnimUtil::CanPlayerFeed(targetInCombat)) {
        SKSE::log::debug("IsValidFeedTarget: false - player can't feed");
        return false;
    }

    // 1.5. Werewolf front-only check: werewolf paired feed only has front animation
    // If player is behind a living target, don't show prompt
    if (TargetState::IsWerewolf(player) && !target->IsDead()) {
        bool isBehind = AnimUtil::GetClosestDirection(target, player);
        if (isBehind) {
            SKSE::log::debug("IsValidFeedTarget: false - werewolf requires front position (player is behind)");
            return false;
        }
    }

    // 2. Check Standard Exclusions (Filters, Paired Animations, etc.)
    if (IsExcluded(target)) {
        SKSE::log::debug("IsValidFeedTarget: false - target excluded");
        return false;
    }

    // 3. Check Distance
    auto playerPos = player->GetPosition();
    auto targetPos = target->GetPosition();
    float dist = playerPos.GetDistance(targetPos);
    SKSE::log::debug("IsValidFeedTarget: player pos ({:.1f}, {:.1f}, {:.1f}), target pos ({:.1f}, {:.1f}, {:.1f}), dist={:.1f}",
        playerPos.x, playerPos.y, playerPos.z, targetPos.x, targetPos.y, targetPos.z, dist);
    if (dist > settings->PromptDisplay.MaxTargetDistance) {
        SKSE::log::debug("IsValidFeedTarget: false - target too far: {:.1f} (max: {:.1f})", dist, settings->PromptDisplay.MaxTargetDistance);
        return false;
    }

    // 4. Check OStim Scenes (Player AND Target)
    if (settings->Filtering.ExcludeOStimScenes) {
        // Check Target
        if (OStimIntegration::IsActorInScene(target)) {
            SKSE::log::debug("IsValidFeedTarget: false - target {} in OStim scene", target->GetName());
            return false;
        }

        // Check Player
        if (OStimIntegration::IsActorInScene(player)) {
            SKSE::log::debug("IsValidFeedTarget: false - player in OStim scene");
            return false;
        }
    }

    SKSE::log::debug("IsValidFeedTarget: true - {} is valid", target->GetName());
    return true;
}

// Event Handlers
void PairedAnimPromptSink::OnCrosshairUpdate(RE::Actor* newTarget) {
    if (g_clientID.load(std::memory_order_acquire) == 0) return;

    // Track last crosshair target for refresh after animations
    if (newTarget) {
        lastCrosshairActor_ = newTarget->GetHandle();
    } else {
        lastCrosshairActor_.reset();
    }

    // Check animation event timeout (safety net)
    AnimEventSink::CheckTimeout();

    // Check if feed animation just ended - force resend prompt
    bool feedJustEnded = FeedAnimState::CheckAndClearFeedEnded();

    bool isValidTarget = false;
    // Check if looking at a valid feed target
    if (newTarget && newTarget != RE::PlayerCharacter::GetSingleton()) {
        if (IsValidFeedTarget(newTarget)) {
            isValidTarget = true;
        }
    }

    auto currentTargetPtr = GetTarget();
    RE::Actor* currentTarget = currentTargetPtr.get();

    if (isValidTarget && newTarget) {
        auto newTargetHandle = newTarget->GetHandle();
        auto settings = Settings::GetSingleton();
        // Use combat-specific delay (default 0) if target is in combat, otherwise general delay
        bool targetInCombat = newTarget->IsInCombat();
        float delaySeconds = targetInCombat ? settings->Combat.PromptDelayCombatSeconds : settings->General.PromptDelayIdleSeconds;

        // Feed just ended - show prompt immediately (no delay)
        if (feedJustEnded) {
            pendingTarget_.reset();
            ShowPrompt(newTarget);
            SKSE::log::info("Showing feed prompt for: {} (FormID: {:X}) (after feed ended)",
                newTarget->GetName(), newTarget->GetFormID());
        }
        // New target - start delay timer
        else if (currentTarget != newTarget && pendingTarget_ != newTargetHandle) {
            pendingTarget_ = newTargetHandle;
            pendingTargetTime_ = std::chrono::steady_clock::now();
            SKSE::log::debug("New target detected: {} - waiting {:.2f}s before showing prompt",
                newTarget->GetName(), delaySeconds);
        }
        // Same pending target - check if delay has elapsed
        else if (pendingTarget_ == newTargetHandle) {
            auto elapsed = std::chrono::steady_clock::now() - pendingTargetTime_;
            float elapsedSeconds = std::chrono::duration<float>(elapsed).count();

            if (elapsedSeconds >= delaySeconds) {
                pendingTarget_.reset();
                ShowPrompt(newTarget);
                SKSE::log::info("Showing feed prompt for: {} (FormID: {:X}) (after {:.2f}s delay)",
                    newTarget->GetName(), newTarget->GetFormID(), elapsedSeconds);
            }
        }
    } else {
        // No valid target or excluded - remove prompt and clear pending
        pendingTarget_.reset();
        if (currentTarget) {
            HidePrompt();
            SKSE::log::debug("Removed feed prompt");
        }
    }
}

void PairedAnimPromptSink::OnMenuStateChange(bool isMenuOpen) {
    if (isMenuOpen) {
        if (GetTarget()) {
            HidePrompt();
            SKSE::log::debug("Menu opened, removing prompt");
        }
    }
}

void PairedAnimPromptSink::OnPeriodicValidation() {
    // Early exit: If player isn't a feeding race (Vampire/Werewolf/VL), skip all validation
    // This avoids expensive IsValidFeedTarget checks when player can't feed
    // Note: When player transforms (via quest), they'll need to look at a target to trigger validation
    // This is acceptable since transformations are rare and player typically needs to re-target anyway
    if (!AnimUtil::IsPlayerFeedingRace()) {
        // If we have an active target, hide the prompt since player can no longer feed
        auto targetPtr = GetTarget();
        if (targetPtr) {
            HidePrompt();
        }
        return;
    }

    auto currentTargetPtr = GetTarget();
    if (currentTargetPtr) {
        RE::Actor* currentTarget = currentTargetPtr.get();

        // // Dummy logging for OStim
        // bool inScene = OStimIntegration::IsActorInScene(currentTarget);
        // SKSE::log::debug("OnPeriodicValidation: Target {} | OStim Scene: {}", currentTarget->GetName(), inScene);

        // Re-validate the current target
        if (!IsValidFeedTarget(currentTarget)) {
            SKSE::log::debug("Target {} became invalid during periodic check", currentTarget->GetName());
            HidePrompt();
        }
    } else {
        // Check if we should restore prompt for last known crosshair target
        // This handles cases where prompt was hidden (e.g. during feed) but is now valid again
        // and RefreshPrompt failed due to race conditions (animation state lagging)
        auto ref = lastCrosshairActor_.get();
        if (ref && ref->Is(RE::FormType::ActorCharacter)) {
             RE::Actor* actor = ref->As<RE::Actor>();
             if (IsValidFeedTarget(actor)) {
                 ShowPrompt(actor);
                 SKSE::log::debug("Prompt restored during periodic check for: {}", actor->GetName());
             }
        }
    }
}

void PairedAnimPromptSink::RefreshPrompt() {
    if (g_clientID.load(std::memory_order_acquire) == 0) return;

    // Logic similar to OnCrosshairUpdate but typically called when we just want to re-evaluate
    // or when we know animation ended.
    
    // First, check if we already have a target
    auto targetPtr = GetTarget();
    RE::Actor* target = targetPtr.get();

    // If not, check what is currently under the crosshair
    if (!target) {
        auto ref = lastCrosshairActor_.get();
        if (ref && ref->Is(RE::FormType::ActorCharacter)) {
            target = ref->As<RE::Actor>();
        }
    }

    if (target) {
        if (IsValidFeedTarget(target)) {
            ShowPrompt(target);
            SKSE::log::debug("Refreshed prompt for target: {}", target->GetName());
        }
    }
}

void PairedAnimPromptSink::ShowPrompt(RE::Actor* target) {
    SetTarget(target);
    bool sent = SkyPromptAPI::SendPrompt(this, g_clientID.load(std::memory_order_acquire));
    if (!sent) {
        SKSE::log::debug("SendPrompt returned false");
    }

    auto* settings = Settings::GetSingleton();
    if (settings->IconOverlay.EnableIconOverlay) {
        FeedIconOverlay::GetSingleton()->ShowIcon(target, settings->IconOverlay.IconPath, 3600.0f);
    }
}

void PairedAnimPromptSink::HidePrompt() {
    SkyPromptAPI::RemovePrompt(this, g_clientID.load(std::memory_order_acquire));

    auto* settings = Settings::GetSingleton();
    if (settings->IconOverlay.EnableIconOverlay) {
        FeedIconOverlay::GetSingleton()->StopIcon();
    }

    SetTarget(nullptr);
}


