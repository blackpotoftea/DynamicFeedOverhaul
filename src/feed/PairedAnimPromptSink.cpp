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
#include "utils/MenuCheck.h"
#include "feed/AnimationRegistry.h"
#include "utils/AnimUtil.h"
#include "utils/IdleParser.h"
#include "feed/WitnessDetection.h"
#include <thread>

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
        SKSE::log::debug("Feed animation started - skipping paired animation exclusion");

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
        SKSE::log::info("Feed animation ended");

        // Always reset time multiplier to normal (safe even if not slowed)
        auto* timer = RE::BSTimer::GetSingleton();
        if (timer) {
            timer->SetGlobalTimeMultiplier(1.0f, true);
        }

        // Clear kill move flag (was set to prevent Quick Loot etc. during animation)
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            AnimUtil::SetInKillMove(player, false);
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
         SKSE::log::debug("[AnimEvent] {}", tag.c_str());
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
}

void PairedAnimPromptSink::UpdateFeedButtons() {
    auto* settings = Settings::GetSingleton();

    // Update button bindings only - prompt is constructed dynamically in SetTarget()
    feedButtons_ = {{
        {RE::INPUT_DEVICE::kKeyboard, static_cast<SkyPromptAPI::ButtonID>(settings->Input.FeedKey)},
        {RE::INPUT_DEVICE::kGamepad, static_cast<SkyPromptAPI::ButtonID>(settings->Input.FeedGamepadKey)}
    }};

    // If there's an active target, refresh the prompt with new buttons
    auto currentTargetPtr = GetTarget();
    if (currentTargetPtr) {
        SKSE::log::debug("UpdateFeedButtons: Refreshing prompt for current target");
        ShowPrompt(currentTargetPtr.get());
    }
}

std::span<const SkyPromptAPI::Prompt> PairedAnimPromptSink::GetPrompts() const {
    return prompts_;
}

void PairedAnimPromptSink::ProcessEvent(SkyPromptAPI::PromptEvent event) const {
    SKSE::log::info("ProcessEvent - eventType: {}, promptType: {}, text: '{}'",
        static_cast<int>(event.type),
        static_cast<int>(event.prompt.type),
        event.prompt.text);

    // Get non-const singleton - ProcessEvent is const due to API contract,
    // but we need to modify state. Using singleton access is the proper pattern here.
    auto* self = GetSingleton();

    switch (event.type) {
    case SkyPromptAPI::PromptEventType::kDown:
        // Button pressed - just log, don't act yet (wait for kUp or kAccepted)
        SKSE::log::debug("kDown event - button pressed (waiting for release or hold completion)");
        break;
    case SkyPromptAPI::PromptEventType::kUp:
        // Button released - for kHold prompts this means quick press (normal feed)
        if (event.prompt.type == SkyPromptAPI::PromptType::kHold) {
            SKSE::log::info("kUp on kHold prompt - button released early, executing normal feed (non-lethal)");
            self->isLethalFeedInProgress_ = false;
            self->HandleFeedAccepted();
        } else {
            SKSE::log::debug("kUp on kSinglePress prompt - ignoring");
        }
        break;
    case SkyPromptAPI::PromptEventType::kAccepted:
        // For kHold: held to completion (lethal feed)
        // For kSinglePress: normal single press
        if (event.prompt.type == SkyPromptAPI::PromptType::kHold) {
            SKSE::log::info("kAccepted on kHold prompt - hold completed, executing lethal feed");
            self->isLethalFeedInProgress_ = true;
        } else {
            SKSE::log::info("kAccepted on kSinglePress prompt - executing normal feed");
        }
        self->HandleFeedAccepted();
        break;
    case SkyPromptAPI::PromptEventType::kTimingOut:
        self->HandleTimingOut();
        break;
    default:
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
    if (CustomFeed::PlayPairedFeed(idleEditorID, target, isPairedAnim)) {
        PapyrusCall::SendOnVampireFeedEvent(target);

        // Send custom DAO_VampireFeed event with attacker and target
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            PapyrusCall::SendDAO_VampireFeedEvent(player, target);
        }

        // Only call vampire script if NOT a werewolf
        if (player && !TargetState::IsWerewolf(player)) {
            auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
            if (vampireQuest) {
                PapyrusCall::CallVampireFeed(vampireQuest, target, isLethal);
            } else {
                SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
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
                if (isLethal && target && !hasOARAnimation) {
                    SKSE::log::info("No OAR animation found - manually killing target after animation");
                    AnimUtil::KillTarget(target);
                } else if (isLethal && hasOARAnimation) {
                    SKSE::log::info("OAR combat animation found - letting animation handle kill");
                }
                break;
        }
    } else {
        SKSE::log::warn("CustomFeed failed");
    }
}

// We have 2 animation systems Vannila Idle and OAR which we set via GraphVariable
// We need both select idle -> set correct graph variable to match OAR animations
void PairedAnimPromptSink::HandleFeedAccepted() {
    auto feedTargetPtr = GetTarget();
    if (!feedTargetPtr) return;

    // Safe to use raw pointer now - NiPointer keeps it alive for entire function scope
    RE::Actor* feedTarget = feedTargetPtr.get();

    // Store the feed target for witness detection (thread-safe)
    SetActiveFeedTarget(feedTarget);

    HidePrompt();

    FeedAnimState::MarkFeedStarted();
    AnimEventSink::Register();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    
    // Mark player as in kill move to prevent Quick Loot and other mods from interfering
    AnimUtil::SetInKillMove(player, true);

    auto* settings = Settings::GetSingleton();
    auto furnitureRef = TargetState::GetFurnitureReference(feedTarget);

    SKSE::log::info("Feed ACCEPTED on target: {} (FormID: {:X})",
        feedTarget->GetName(), feedTarget->GetFormID());

    if (settings->IconOverlay.EnableIconOverlay) {
        // Trigger bite animation instead of just stopping
        FeedIconOverlay::GetSingleton()->TriggerFeedAnimation();
    }

    bool isInCombat = false;
    int targetState = AnimUtil::DetermineTargetState(feedTarget, isInCombat);
    SKSE::log::debug("Target state: {} (combat={})", targetState, isInCombat);

    int vampireStage = PapyrusCall::GetVampireStage();
    bool useTwoSingle = settings->NonCombat.UseTwoSingleAnimations && targetState == AnimUtil::kStanding;

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

            if (targetState == AnimUtil::kStanding && settings->NonCombat.EnableHeightAdjust) {
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

            if (targetState == AnimUtil::kStanding && settings->NonCombat.EnableRotation) {
                AnimUtil::RotateTargetToClosest(target, player);
                AnimUtil::RotateAttackerToTarget(player, target);
            }
        });

        // Calculate direction for animation selection (can be done immediately)
        bool isBehind = false;
        if (targetState == AnimUtil::kStanding) {
            isBehind = AnimUtil::GetClosestDirection(feedTarget, player);
        } else {
            // For sitting/sleeping, just detect direction without rotating
            isBehind = AnimUtil::GetClosestDirection(feedTarget, player);
        }

        // --- New Registry Logic ---
        Feed::FeedContext context;
        context.player = player;
        context.target = feedTarget;
        context.isCombat = isInCombat;
        context.isSneaking = player->IsSneaking();
        context.isHungry = (vampireStage >= settings->Animation.HungryThreshold);
        context.targetIsStanding = (targetState == AnimUtil::kStanding);
        context.isBehind = isBehind;
        context.isLethal = isLethalFeedInProgress_;  // Pass user's lethal feed choice to animation selection

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

        if (settings->General.ForceFeedType > 0){
            feedType = settings->General.ForceFeedType;
            SKSE::log::info("Animation override set ");
        }


        SKSE::log::info("Registry match: {} (Type: {}, Lethal: {})", animName, feedType, isLethal);

        // Check if we found a valid OAR animation (not "Default")
        bool hasOARAnimation = (anim != nullptr && animName != "Default");

        bool isPairedAnim = true;
        const char* idleEditorID = AnimUtil::SelectIdleAnimation(targetState, feedTarget, furnitureRef, isBehind, isPairedAnim);

        AnimUtil::SetFeedGraphVars(player, feedType);
        AnimUtil::SetFeedGraphVars(feedTarget, feedType);

        ExecuteFeed(idleEditorID, feedTarget, isPairedAnim, isLethal, hasOARAnimation);

        // Reset lethal flag after use
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

    // Construct prompt dynamically if needed to ensure keys are up to date
    // But we update buttons on init or update, here we just attach target
    if (target) {
        auto* settings = Settings::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();

        // Determine prompt type based on combat state
        SkyPromptAPI::PromptType promptType = SkyPromptAPI::PromptType::kSinglePress;
        std::string promptText = "Feed";
        float progressValue = 0.0f;
        uint32_t textColor = 0xFFFFFFFF;  // White (AABBGGRR format)

        // Check if target is dead - use different prompt for corpses
        bool targetIsDead = target->IsDead();

        if (targetIsDead) {
            // Dead targets get simple "Drain Corpse" prompt (no hold-to-kill option)
            promptText = "Drain Corpse";
            SKSE::log::info("SetTarget: Using corpse prompt - target: {}", target->GetName());
        } else {
            // Check if target is NOT in combat and lethal feed is enabled
            bool targetInCombat = target->IsInCombat();
            bool playerInCombat = player && player->IsInCombat();
            bool isEssential = TargetState::IsEssentialOrProtected(target);

            // Show kill prompt only if: not in combat, lethal feed enabled, and not excluded Essential actor
            bool canShowLethalPrompt = !targetInCombat && !playerInCombat && settings->NonCombat.EnableLethalFeed;
            if (canShowLethalPrompt && settings->NonCombat.ExcludeEssentialFromLethal && isEssential) {
                canShowLethalPrompt = false;
                SKSE::log::info("SetTarget: Excluding Essential actor {} from lethal prompt", target->GetName());
            }

            if (canShowLethalPrompt) {
                // Use HOLD prompt for non-combat (lethal feed)
                promptType = SkyPromptAPI::PromptType::kHold;
                promptText = "Feed (Hold to Kill)";
                progressValue = settings->NonCombat.LethalHoldDuration;
                textColor = 0xFF5555FF;  // Red warning color for lethal option
                SKSE::log::info("SetTarget: Using kHold prompt (lethal feed enabled) - target: {}, duration: {}s",
                    target->GetName(), progressValue);
            } else {
                SKSE::log::info("SetTarget: Using kSinglePress prompt - target: {}, targetInCombat: {}, playerInCombat: {}, isEssential: {}",
                    target->GetName(), targetInCombat, playerInCombat, isEssential);
            }
        }

        prompts_[0] = SkyPromptAPI::Prompt(
            promptText,
            1, 1, promptType,
            target->GetFormID(),
            feedButtons_,
            textColor,
            progressValue  // progress threshold for hold
        );
    } else {
        // Clear prompt array to avoid stale FormID
        prompts_[0] = SkyPromptAPI::Prompt();
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
    if (!actor) return true;

    auto* settings = Settings::GetSingleton();
    if (!settings->General.EnableMod) return true;

    if (FeedAnimState::IsFeedActive()) {
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

    // 0.7. Check if player is facing target (camera can rotate independently in 3rd person)
    if (settings->PromptDisplay.RequirePlayerFacing) {
        if (!AnimUtil::IsPlayerFacingTarget(player, target, settings->PromptDisplay.FacingAngleThreshold)) {
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

    // 2. Check Standard Exclusions (Filters, Paired Animations, etc.)
    if (IsExcluded(target)) {
        SKSE::log::debug("IsValidFeedTarget: false - target excluded");
        return false;
    }

    // 3. Check Distance (Max 250 units)
    float dist = player->GetPosition().GetDistance(target->GetPosition());
    if (dist > 250.0f) {
        SKSE::log::debug("IsValidFeedTarget: false - target too far: {:.1f}", dist);
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

            // Debug: Find and log which kill move would match for this target
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                IdleParser::DebugFindKillMove(player, newTarget);
            }
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


