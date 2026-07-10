#include "PCH.h"
#include "feed/FeedPromptSink.h"
#include "feed/AnimEventSink.h"
#include "Settings.h"
#include "feed/TargetState.h"
#include "papyrus/PapyrusCall.h"
#include "feed/PairedAnimation.h"
#include "feed/FeedFiltering.h"
#include "feed/CompositePairedAnimation.h"
#include "feed/FeedIconOverlay.h"
#include "integration/OStimIntegration.h"
#include "integration/SexLabIntegration.h"
#include "integration/VampireIntegrationUtils.h"
#include "utils/MenuCheck.h"
#include "feed/AnimationRegistry.h"
#include "utils/AnimUtil.h"
#include "feed/WitnessDetection.h"
#include <thread>
#include <algorithm>
#include <cmath>
#include <random>

extern std::atomic<SkyPromptAPI::ClientID> g_clientID;

namespace {
    // True when the player's vampire stage has reached the "hungry" threshold.
    // Centralizes the stage-vs-setting comparison used by both the composite
    // resolver and the legacy feed path.
    bool PlayerIsHungry() {
        return PapyrusCall::GetVampireStage() >= Settings::GetSingleton()->Animation.HungryThreshold;
    }

    // Fills the EnterFeedState settings-derived fields (height/rotation) from
    // Settings so the composite and legacy paths can't drift on them. Only
    // feedType differs between the two callers.
    void EnterFeedStateFromSettings(RE::Actor* player, RE::Actor* target,
            int feedType, int targetState, bool playerInCombat, bool targetInCombat) {
        auto* settings = Settings::GetSingleton();
        PairedAnimation::EnterFeedState({
            player, target, feedType, targetState,
            playerInCombat, targetInCombat,
            settings->NonCombat.EnableHeightAdjust,
            settings->NonCombat.EnableRotation,
            settings->NonCombat.MinHeightDiff,
            settings->NonCombat.MaxHeightDiff,
        });
    }

    // Grace before a corpse prompt appears. Longer than the idle delay so a fresh
    // kill (last-enemy feed ends combat, unmasking the body) can't be drained by a
    // still-held feed key.
    constexpr float kCorpseFeedGraceSeconds = 1.0f;

    // Combat-aware prompt delay: combat targets use the (typically shorter)
    // combat delay, everyone else the idle delay; corpses use the grace above.
    float PromptDelayForTarget(RE::Actor* target) {
        auto* settings = Settings::GetSingleton();
        if (target->IsDead()) return kCorpseFeedGraceSeconds;
        return target->IsInCombat() ? settings->Combat.PromptDelayCombatSeconds
                                     : settings->General.PromptDelayIdleSeconds;
    }

    // Seconds elapsed since a steady_clock timestamp.
    float SecondsSince(std::chrono::steady_clock::time_point since) {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - since).count();
    }

    // Resolve an ObjectRefHandle to a live Actor NiPointer, or nullptr.
    RE::NiPointer<RE::Actor> ActorFromHandle(const RE::ObjectRefHandle& handle) {
        auto ref = handle.get();
        if (!ref) {
            return nullptr;
        }
        return RE::NiPointer<RE::Actor>(ref->As<RE::Actor>());
    }
}

// FeedPromptSink Implementation

FeedPromptSink* FeedPromptSink::GetSingleton() {
    static FeedPromptSink singleton;
    return &singleton;
}

FeedPromptSink::FeedPromptSink() {
    UpdateFeedButtons();
    RegisterCorePromptCallback();
}

void FeedPromptSink::RegisterCorePromptCallback() {
    RegisterPromptCallback([](RE::Actor* target) -> std::vector<PromptDef> {
        std::vector<PromptDef> prompts;
        if (!target) return prompts;

        // Active-composite-feed toggle: when this is the currently-feeding
        // target AND we're on the composite path, the only prompt is "Stop
        // Feed" bound to the same key. Gated on CompositePairedAnimation::
        // IsActive() (NOT IsFeedActive) so the legacy single-actor
        // PairedAnimation::ExecuteFeed path retains its original behavior
        // (prompt hidden, no toggle).
        if (CompositePairedAnimation::IsActive()) {
            auto activeFeed = FeedPromptSink::GetSingleton()->GetActiveFeedTarget();
            if (activeFeed && activeFeed.get() == target) {
                prompts.push_back({
                    .text = "Stop Feed",
                    .type = SkyPromptAPI::PromptType::kSinglePress,
                    .color = 0xFFCCCCFFu,
                    .priority = 1000,
                    .onAccept = nullptr
                });
                return prompts;
            }
        }

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
                    FeedPromptSink::GetSingleton()->isLethalFeedInProgress_ = true;
                }
            });
        }
        else {
            // Non-combat. If this feed will run as a composite (staged drain-toggle)
            // animation, the kill is governed by how long the player feeds, not by a
            // hold-to-kill choice — so show a plain single-press "Feed" and let the
            // composite own lethality. Only the legacy fallback path uses the
            // EnableLethalFeed hold-to-kill prompt.
            bool willUseComposite = player &&
                CompositePairedAnimation::Resolve(player, target).pack != nullptr;

            bool canLethal = settings->NonCombat.EnableLethalFeed &&
                            !(settings->NonCombat.ExcludeEssentialFromLethal && isEssential);

            if (willUseComposite) {
                prompts.push_back({
                    .text = "Feed",
                    .type = SkyPromptAPI::PromptType::kSinglePress,
                    .color = 0xFFFFFFFF,
                    .priority = 1000,
                    .onAccept = nullptr  // composite controls lethality via the drain toggle
                });
            } else if (canLethal) {
                prompts.push_back({
                    .text = "Feed (Hold to Kill)",
                    .type = SkyPromptAPI::PromptType::kHold,
                    .holdDuration = settings->NonCombat.LethalHoldDuration,
                    .color = 0xFF5555FF,  // Red warning
                    .priority = 1000,
                    .onAccept = [](RE::Actor*, bool holdComplete) {
                        // Only set state - ProcessEvent calls HandleFeedAccepted
                        FeedPromptSink::GetSingleton()->isLethalFeedInProgress_ = holdComplete;
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

void FeedPromptSink::UpdateFeedButtons() {
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

void FeedPromptSink::RegisterPromptCallback(PromptCallback callback) {
    promptCallbacks_.push_back(std::move(callback));
    SKSE::log::info("Registered prompt callback (total: {})", promptCallbacks_.size());
}

std::span<const SkyPromptAPI::Prompt> FeedPromptSink::GetPrompts() const {
    return prompts_;
}

void FeedPromptSink::ProcessEvent(SkyPromptAPI::PromptEvent event) const {
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
            // Defensive composite-only toggle gate: during an active composite
            // feed the prompt is kSinglePress ("Stop Feed"), so kUp on kHold
            // shouldn't normally fire — but a lingering kHold release at the
            // moment the composite feed started would otherwise re-trigger
            // HandleFeedAccepted.
            if (CompositePairedAnimation::IsActive()) {
                SKSE::log::info("[Toggle] kUp during active composite feed - requesting exit");
                // Play the graceful exit animation; teardown + MarkFeedEnded
                // happen when the exit clip finishes (driven by Tick()).
                CompositePairedAnimation::RequestStop();
                return;
            }
            SKSE::log::info("kUp on kHold - early release, executing non-lethal feed");
            if (matchedDef->onAccept) matchedDef->onAccept(target, false);
            self->HandleFeedAccepted();
        }
        break;

    case SkyPromptAPI::PromptEventType::kAccepted:
        {
            // Composite-only toggle: if a composite feed is running, this
            // press exits instead of starting a new one. Stops the composite,
            // then chains into MarkFeedEnded for the state teardown. The
            // legacy single-actor PairedAnimation::ExecuteFeed path is not
            // touched — its prompt is hidden during play, so this branch
            // can't fire anyway, but the gate also makes the rule explicit.
            if (CompositePairedAnimation::IsActive()) {
                SKSE::log::info("[Toggle] Feed key pressed during active composite feed - requesting exit");
                // Play the graceful exit animation; teardown + MarkFeedEnded
                // happen when the exit clip finishes (driven by Tick()).
                CompositePairedAnimation::RequestStop();
                return;
            }
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




// We have 2 animation systems Vannila Idle and OAR which we set via GraphVariable
// We need both select idle -> set correct graph variable to match OAR animations
void FeedPromptSink::HandleFeedAccepted() {
    auto feedTargetPtr = GetTarget();
    if (!feedTargetPtr) return;

    // Safe to use raw pointer now - NiPointer keeps it alive for entire function scope
    RE::Actor* feedTarget = feedTargetPtr.get();

    // One-shot snapshot of the lethal-hold flag, cleared immediately so a stale
    // `true` can't leak into a later feed. Only the legacy path consults it; the
    // composite path returns early (and never reset it before), and the non-lethal
    // "Feed" prompt's onAccept doesn't reset it either.
    const bool wantLethal = isLethalFeedInProgress_;
    isLethalFeedInProgress_ = false;

    auto* settings = Settings::GetSingleton();

    // Store the feed target for witness detection (thread-safe)
    SetActiveFeedTarget(feedTarget);

    // Hide the existing prompt; the composite branch re-shows it as
    // "Stop Feed" AFTER CompositePairedAnimation::Play sets isActive_=true
    // (so the callback's CompositePairedAnimation::IsActive() check fires).
    // The single-actor branch leaves the prompt hidden, preserving the
    // original behavior (no toggle UX for that path).
    HidePrompt();
    FeedAnimState::MarkFeedStarted();
    AnimEventSink::Register();

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    auto furnitureRef = TargetState::GetFurnitureReference(feedTarget);

    SKSE::log::info("Feed ACCEPTED on target: {} (FormID: {:X})",
        feedTarget->GetName(), feedTarget->GetFormID());

    if (settings->IconOverlay.EnableIconOverlay) {
        // Trigger bite animation instead of just stopping
        FeedIconOverlay::GetSingleton()->TriggerFeedAnimation();
    }

    bool isInCombat = false;  // Target's combat state
    AnimUtil::DetermineTargetState(feedTarget, isInCombat);  // sets isInCombat; targetState comes from the resolver

    // Player's combat state forces lethal feed
    bool playerInCombat = player->IsInCombat();

    // Resolve the composite pack (and the geometry/furniture context it implies)
    // through the SAME helper the prompt callback uses, so the prompt label and the
    // actual feed path can never disagree on whether this is a composite feed. With
    // no matching pack the resolver returns nullptr and we fall through to the legacy
    // single-actor path below.
    auto resolution = CompositePairedAnimation::Resolve(player, feedTarget);
    int targetState = resolution.targetState;
    bool geometryBehind = resolution.geometryBehind;
    bool isBehind = resolution.isBehind;
    bool isFurnitureFeed = resolution.isFurnitureFeed;
    const Feed::CompositePack* compositePack = resolution.pack;
    const bool useComposite = (compositePack != nullptr);

    SKSE::log::debug("Target state: {} (targetCombat={}, playerCombat={})", targetState, isInCombat, playerInCombat);

    if (useComposite) {
        // Composite path: two single-actor animations played in sync, with the
        // pair locked via Skyrim's native TranslateTo (set up in
        // CompositePairedAnimation::Play).
        PairedAnimation::SetFeedTarget(feedTarget);
        EnterFeedStateFromSettings(player, feedTarget, /*feedType=*/0, targetState,
                                   playerInCombat, isInCombat);

        // EnterFeedState's auto front/back (RotateTargetToClosest) may have turned
        // the victim to face away. When forcing front (no Back pack loaded),
        // re-face the victim to the player; idempotent if it already faces front.
        // Upright feeds only — a furniture victim is left exactly as it lies.
        if (!isFurnitureFeed && settings->NonCombat.EnableRotation && geometryBehind && !isBehind) {
            AnimUtil::RotateTargetToReference(feedTarget, player, /*faceAway=*/false);
        }

        FeedAnimState::SetCurrentFeedLethal(false);
        // Composite owns its own kill (KillTarget inline in the Loop when the
        // victim is drained dry), so flag hasOAR=true to suppress the vanilla
        // manual-kill fallback in FeedIntegration::Run.
        FeedAnimState::SetFeedHasOAR(true);

        Feed::CompositePack pack = *compositePack;
        if (isFurnitureFeed) {
            SKSE::log::info("[HandleFeedAccepted] Furniture composite pack '{}' selected (furniture={}, dir={}, playerOnly={})",
                pack.name, static_cast<int>(pack.furniture), static_cast<int>(pack.direction), pack.playerOnly);
        } else {
            SKSE::log::info("[HandleFeedAccepted] Standing composite pack '{}' selected (isBehind={}, geometryBehind={})",
                pack.name, isBehind, geometryBehind);
        }

        if (!CompositePairedAnimation::Play(feedTarget, pack)) {
            SKSE::log::warn("[HandleFeedAccepted] CompositePairedAnimation::Play failed - tearing down feed state");
            FeedAnimState::MarkFeedEnded();
            return;
        }
        // Play() has set isActive_=true — now the prompt callback returns
        // "Stop Feed". Re-show the prompt so the user sees the toggle UI.
        ShowPrompt(feedTarget);
        return;
    } else {
        // Calculate direction for animation selection (can be done immediately).
        // Same detection for standing and sitting/sleeping — neither rotates here.
        bool isBehind = AnimUtil::GetClosestDirection(feedTarget, player);

        // --- New Registry Logic ---
        Feed::FeedContext context;
        context.player = player;
        context.target = feedTarget;
        context.isCombat = playerInCombat;  // Use PLAYER's combat state (forces lethal animations)
        context.isSneaking = player->IsSneaking();
        context.isHungry = PlayerIsHungry();
        context.targetIsStanding = (targetState == Feed::kStanding);
        context.isBehind = isBehind;
        context.isLethal = wantLethal || playerInCombat;  // User choice OR forced by player combat

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
        if (wantLethal) {
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

        // Centralized per-actor setup: kill-move flag, conditional pacify,
        // height/rotation positioning, graph vars. Symmetric teardown lives
        // in PairedAnimation::ExitFeedState (called from OnComplete).
        EnterFeedStateFromSettings(player, feedTarget, feedType, targetState,
                                   playerInCombat, isInCombat);

        // Publish lethal state so the VFD_VampireFeedTrigger event handler can
        // decide variance vs fixed drain. Cleared on MarkFeedEnded/MarkFeedStarted.
        FeedAnimState::SetCurrentFeedLethal(isLethal);
        // Stash hasOAR for the centralized FeedIntegration::Run fired in MarkFeedEnded.
        FeedAnimState::SetFeedHasOAR(hasOARAnimation);

        PairedAnimation::ExecuteFeed(idleEditorID, feedTarget, isPairedAnim, isLethal, hasOARAnimation);
        // (lethal flag already cleared at the top of HandleFeedAccepted)
    }
}

void FeedPromptSink::HandleTimingOut() {
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
void FeedPromptSink::SetTargetHandle(const RE::ObjectRefHandle& handle) {
    std::lock_guard<std::mutex> lock(targetMutex_);
    currentTargetHandle_ = handle;
}

RE::ObjectRefHandle FeedPromptSink::GetTargetHandle() const {
    std::lock_guard<std::mutex> lock(targetMutex_);
    return currentTargetHandle_;
}

// Thread-safe wrapper methods for activeFeedTargetHandle_
void FeedPromptSink::SetActiveFeedTarget(RE::Actor* target) {
    std::lock_guard<std::mutex> lock(targetMutex_);
    if (target) {
        activeFeedTargetHandle_ = target->GetHandle();
    } else {
        activeFeedTargetHandle_.reset();
    }
}

RE::NiPointer<RE::Actor> FeedPromptSink::GetActiveFeedTarget() const {
    std::lock_guard<std::mutex> lock(targetMutex_);
    return ActorFromHandle(activeFeedTargetHandle_);
}

void FeedPromptSink::SetTarget(RE::Actor* target) {
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

RE::NiPointer<RE::Actor> FeedPromptSink::GetTarget() const {
    // NiPointer<Actor> keeps ref alive in the caller's scope
    return ActorFromHandle(GetTargetHandle());
}

bool FeedPromptSink::IsExcluded(RE::Actor* actor) {
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
        // Composite path: allow the currently-feeding target through so its
        // "Stop Feed" prompt stays visible. Other actors are still excluded.
        // Non-composite path: original blanket exclude (no toggle UX).
        if (CompositePairedAnimation::IsActive()) {
            auto activeFeed = GetSingleton()->GetActiveFeedTarget();
            if (!activeFeed || activeFeed.get() != actor) {
                SKSE::log::debug("IsExcluded: composite feed active on different target");
                return true;
            }
            // Fall through — active feed's own target proceeds to prompt callbacks.
        } else {
            SKSE::log::debug("IsExcluded: feed already active (non-composite path)");
            return true;
        }
    }
    
    // Check common filters first (fast)
    if (FeedFiltering::IsExcludedByFilters(actor)) return true;

    // Skip corpses while the player is in combat — combat feeds target living enemies only.
    auto* playerForCombatCheck = RE::PlayerCharacter::GetSingleton();
    if (actor->IsDead() && playerForCombatCheck && playerForCombatCheck->IsInCombat()) {
        SKSE::log::debug("Excluded: {} - dead body skipped (player in combat)", actor->GetName());
        return true;
    }

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

bool FeedPromptSink::IsValidFeedTarget(RE::Actor* target) {
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
    SKSE::log::trace("IsValidFeedTarget: player pos ({:.1f}, {:.1f}, {:.1f}), target pos ({:.1f}, {:.1f}, {:.1f}), dist={:.1f}",
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

    // 5. Check SexLab Scenes (Player AND Target)
    if (settings->Filtering.ExcludeSexLabScenes) {
        // Check Target
        if (SexLabIntegration::IsActorInScene(target)) {
            SKSE::log::debug("IsValidFeedTarget: false - target {} in SexLab scene", target->GetName());
            return false;
        }

        // Check Player
        if (SexLabIntegration::IsActorInScene(player)) {
            SKSE::log::debug("IsValidFeedTarget: false - player in SexLab scene");
            return false;
        }
    }

    SKSE::log::debug("IsValidFeedTarget: true - {} is valid", target->GetName());
    return true;
}

// Event Handlers
void FeedPromptSink::OnCrosshairUpdate(RE::Actor* newTarget) {
    if (g_clientID.load(std::memory_order_acquire) == 0) return;

    // While a staged composite feed is running the on-screen prompt is the
    // "Stop Feed" toggle. Keep it shown no matter where the camera/crosshair
    // points — don't hide or re-evaluate it (and don't run the legacy anim
    // timeout, which would prematurely end a long feeding loop). The feed's own
    // Tick() drives its lifecycle, and the prompt is refreshed in MarkFeedEnded.
    if (CompositePairedAnimation::IsActive()) return;

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
        // Use combat-specific delay (default 0) if target is in combat, otherwise general delay
        float delaySeconds = PromptDelayForTarget(newTarget);

        // Feed just ended - a living target (still sippable) re-shows immediately.
        // A fresh corpse must not: combat just ended so it's no longer combat-
        // excluded, and the feed key may still be held. Restart the pending timer
        // so the corpse grace elapses before it can be drained.
        if (feedJustEnded) {
            if (newTarget->IsDead()) {
                pendingTarget_ = newTargetHandle;
                pendingTargetTime_ = std::chrono::steady_clock::now();
            } else {
                pendingTarget_.reset();
                ShowPrompt(newTarget);
                SKSE::log::info("Showing feed prompt for: {} (FormID: {:X}) (after feed ended)",
                    newTarget->GetName(), newTarget->GetFormID());
            }
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
            float elapsedSeconds = SecondsSince(pendingTargetTime_);

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

void FeedPromptSink::TickPendingPrompt() {
    if (g_clientID.load(std::memory_order_acquire) == 0) return;
    if (!pendingTarget_) return;

    auto ref = pendingTarget_.get();
    if (!ref || !ref->Is(RE::FormType::ActorCharacter)) {
        pendingTarget_.reset();
        return;
    }

    RE::Actor* actor = ref->As<RE::Actor>();

    float delaySeconds = PromptDelayForTarget(actor);

    float elapsedSeconds = SecondsSince(pendingTargetTime_);

    if (elapsedSeconds < delaySeconds) return;

    // Re-validate before showing (target may have died, moved out of range, etc.)
    if (!IsValidFeedTarget(actor)) {
        pendingTarget_.reset();
        return;
    }

    pendingTarget_.reset();
    ShowPrompt(actor);
    SKSE::log::info("Showing feed prompt for: {} (FormID: {:X}) (after {:.2f}s delay)",
        actor->GetName(), actor->GetFormID(), elapsedSeconds);
}

void FeedPromptSink::OnMenuStateChange(bool isMenuOpen) {
    if (isMenuOpen) {
        if (GetTarget()) {
            HidePrompt();
            SKSE::log::debug("Menu opened, removing prompt");
        }
        return;
    }

    // Menu closed. During a composite feed the "Stop Feed" toggle is restored
    // only here: OnCrosshairUpdate / OnPeriodicValidation early-return while a
    // feed is active, so without this the prompt stays hidden for the rest of
    // the feed after opening/closing a menu. Non-feed prompts self-heal via
    // OnPeriodicValidation, so they're left alone.
    if (!CompositePairedAnimation::IsActive()) return;

    // A nested blocked menu may still be open (or the game still paused) when
    // an inner menu closes — wait for the outermost menu's close event.
    if (MenuCheck::IsAnyBlockedMenuOpen()) return;

    if (auto activeFeed = GetActiveFeedTarget()) {
        ShowPrompt(activeFeed.get());
        SKSE::log::debug("Menu closed during composite feed - restored Stop Feed prompt");
    }
}

void FeedPromptSink::OnPeriodicValidation() {
    // Don't invalidate/hide the prompt mid composite feed — the "Stop Feed"
    // toggle must stay shown even when the crosshair is off the NPC. The feed
    // ends via its own Tick() (player Stop or drained dry), not this check.
    if (CompositePairedAnimation::IsActive()) {
        // Self-heal: if a blocked menu hid the toggle (OnMenuStateChange) and has
        // since closed, restore it. This backstops the menu-close event, whose
        // guard can transiently see the game still paused during the close frame.
        if (!GetTarget() && !MenuCheck::IsAnyBlockedMenuOpen()) {
            if (auto activeFeed = GetActiveFeedTarget()) {
                ShowPrompt(activeFeed.get());
                SKSE::log::debug("Restored Stop Feed prompt after menu close (periodic self-heal)");
            }
        }
        return;
    }

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

void FeedPromptSink::RefreshPrompt() {
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

void FeedPromptSink::ShowPrompt(RE::Actor* target) {
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

void FeedPromptSink::HidePrompt() {
    SkyPromptAPI::RemovePrompt(this, g_clientID.load(std::memory_order_acquire));

    auto* settings = Settings::GetSingleton();
    if (settings->IconOverlay.EnableIconOverlay) {
        FeedIconOverlay::GetSingleton()->StopIcon();
    }

    SetTarget(nullptr);
}

void FeedPromptSink::ResetForLoad() {
    HidePrompt();
    SetActiveFeedTarget(nullptr);
    lastCrosshairActor_ = {};
    pendingTarget_ = {};
    isLethalFeedInProgress_ = false;
    isEmbraceFeedInProgress_ = false;
    ResetTimers();
}


