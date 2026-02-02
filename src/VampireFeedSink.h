#pragma once
#include "SkyPrompt/API.hpp"
#include "Settings.h"
#include "TargetState.h"
#include "PapyrusCall.h"
#include "util.h"
#include "feed/FeedState.h"
#include "feed/CustomFeed.h"
#include "feed/FeedFiltering.h"
#include <mutex>
#include <atomic>

extern SkyPromptAPI::ClientID g_clientID;

// Forward declaration
class VampireFeedSink;

// Forward declaration for refresh function
void RefreshFeedPromptAfterAnimation();

// Global flag set by animation event when feed ends
namespace FeedAnimState {
    inline std::atomic<bool> feedEnded{false};
    inline std::atomic<bool> feedActive{false};  // True while feed animation is in progress

    inline void MarkFeedStarted() {
        feedActive.store(true);
        SKSE::log::debug("Feed animation started - skipping paired animation exclusion");
    }

    inline void MarkFeedEnded() {
        feedEnded.store(true);
        feedActive.store(false);
        SKSE::log::info("Feed animation ended");
        // Restore player control after custom feed animation
        CustomFeed::OnComplete();
        // Directly trigger prompt refresh since crosshair event won't fire
        RefreshFeedPromptAfterAnimation();
    }

    inline bool CheckAndClearFeedEnded() {
        return feedEnded.exchange(false);
    }

    // Returns true if feed is active (skip paired animation exclusion)
    inline bool IsFeedActive() {
        return feedActive.load();
    }
}

// Animation event sink to detect PairEnd (paired animation finished)
class AnimEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
    static AnimEventSink* GetSingleton() {
        static AnimEventSink singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::BSAnimationGraphEvent* event,
        [[maybe_unused]] RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) override
    {
        if (!event || !event->tag.data()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const char* tag = event->tag.data();

        // TEMP: Log all animation events for debugging
        SKSE::log::info("[AnimEvent] {}", tag);

        // PairEnd - fires when paired animation finishes (standing/sitting/combat feeds)
        // IdleStop - fires when solo idle ends (bed/bedroll feeds)
        if (std::strcmp(tag, "PairEnd") == 0 || std::strcmp(tag, "IdleStop") == 0) {
            SKSE::log::info("{} detected - unregistering sink", tag);
            registeredTime_ = std::chrono::steady_clock::time_point{};  // Reset timer
            FeedAnimState::MarkFeedEnded();
            Unregister();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    static void Register() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            // Remove first in case already registered (prevents double registration)
            player->RemoveAnimationGraphEventSink(GetSingleton());
            player->AddAnimationGraphEventSink(GetSingleton());
            registeredTime_ = std::chrono::steady_clock::now();
            SKSE::log::info("Animation event sink registered");
        }
    }

    static void Unregister() {
        registeredTime_ = std::chrono::steady_clock::time_point{};  // Reset timer
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->RemoveAnimationGraphEventSink(GetSingleton());
            SKSE::log::debug("Animation event sink unregistered");
        }
    }

    // Check if timeout has elapsed (call from crosshair handler periodically)
    static void CheckTimeout() {
        if (registeredTime_.time_since_epoch().count() == 0) return;  // Not registered

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - registeredTime_).count();

        if (elapsed >= 15) {
            SKSE::log::warn("Animation event timeout (15s) - forcing prompt refresh");
            registeredTime_ = std::chrono::steady_clock::time_point{};  // Reset
            FeedAnimState::MarkFeedEnded();
            Unregister();
        }
    }

private:
    AnimEventSink() = default;
    inline static std::chrono::steady_clock::time_point registeredTime_{};
};

// Check if an actor is currently in a paired/synced animation
// Uses behavior graph variables which are more reliable than GetPairedAnimation condition
namespace PairedAnimation {
    inline bool IsInPairedAnimation(RE::Actor* actor) {
        if (!actor) return false;

        bool result = false;

        // bIsSynced - true during synced/paired animations (vampire feed, etc.)
        actor->GetGraphVariableBool("bIsSynced", result);
        if (result) return true;

        // bInKillMove - true during killmove animations
        actor->GetGraphVariableBool("bInKillMove", result);
        if (result) return true;

        return false;
    }
}

// Graph variable names for OAR conditions (injected by Behavior Data Injector)
namespace GraphVars {
    // Bool: true when vampire feed is active
    inline constexpr auto IsSkyPromptFeeding = "IsSkyPromptFeeding";
    // Int: composite feed type (TargetState * 10 + HungerStage)
    inline constexpr auto SkyPromptFeedType = "SkyPromptFeedType";
}

class VampireFeedSink : public SkyPromptAPI::PromptSink {
public:
    static VampireFeedSink* GetSingleton() {
        static VampireFeedSink singleton;
        return &singleton;
    }

    std::span<const SkyPromptAPI::Prompt> GetPrompts() const override {
        return prompts_;
    }

    void ProcessEvent(SkyPromptAPI::PromptEvent event) const override {
        SKSE::log::debug("ProcessEvent called - type: {}", static_cast<int>(event.type));

        switch (event.type) {
        case SkyPromptAPI::PromptEventType::kAccepted:
            if (currentTarget_) {
                // Remove the prompt immediately when feed starts
                SkyPromptAPI::RemovePrompt(this, g_clientID);

                // Mark feed as active (skip paired animation exclusion until PairEnd)
                FeedAnimState::MarkFeedStarted();

                // Register animation event sink to detect when feed ends
                AnimEventSink::Register();

                // Keep currentTarget_ - animation event (VampireFeedEnd) will trigger prompt re-show
                RE::Actor* feedTarget = currentTarget_;

                SKSE::log::info("Feed ACCEPTED on target: {} (FormID: {:X})",
                    feedTarget->GetName(), feedTarget->GetFormID());

                auto player = RE::PlayerCharacter::GetSingleton();
                if (!player) break;

                // Check target state
                auto sitSleepState = TargetState::GetSitSleepState(feedTarget);
                bool isInFurniture = TargetState::IsInFurnitureState(feedTarget);
                auto furnitureType = TargetState::GetActorFurnitureType(feedTarget);
                auto furnitureRef = TargetState::GetFurnitureReference(feedTarget);

                SKSE::log::info("Target state: {} | InFurniture: {} | FurnitureType: {}",
                    TargetState::SitSleepStateToString(sitSleepState),
                    isInFurniture,
                    TargetState::FurnitureTypeToString(furnitureType));

                // Determine target state for feed type calculation
                int targetState = FeedState::kStanding;
                bool isInCombat = feedTarget->IsInCombat();

                if (isInCombat) {
                    SKSE::log::info("Target is in COMBAT - using combat feed");
                    targetState = FeedState::kCombat;
                } else if (TargetState::IsSleeping(feedTarget)) {
                    SKSE::log::info("Target is SLEEPING - using bed feed");
                    targetState = FeedState::kSleeping;
                } else if (TargetState::IsSitting(feedTarget)) {
                    SKSE::log::info("Target is SITTING - using seated feed");
                    targetState = FeedState::kSitting;
                } else if (TargetState::IsStanding(feedTarget)) {
                    SKSE::log::info("Target is STANDING - using standing feed");
                    targetState = FeedState::kStanding;

                    // Pre-position to fix stairs animation issue
                    // Move the lower actor up to the higher one's Z position
                    auto* settings = Settings::GetSingleton();
                    if (settings->NonCombat.EnableHeightAdjust) {
                        auto playerPos = player->GetPosition();
                        auto targetPos = feedTarget->GetPosition();
                        float heightDiff = std::fabs(targetPos.z - playerPos.z);

                        if (heightDiff > settings->NonCombat.MinHeightDiff &&
                            heightDiff <= settings->NonCombat.MaxHeightDiff) {
                            float higherZ = std::max(playerPos.z, targetPos.z);

                            if (playerPos.z < targetPos.z) {
                                // Player is lower - move player up to target
                                SKSE::log::info("Height diff: {:.1f} - moving player up from {:.1f} to {:.1f}",
                                    heightDiff, playerPos.z, higherZ);
                                player->SetPosition(RE::NiPoint3(playerPos.x, playerPos.y, higherZ), true);
                            } else {
                                // Target is lower - move target up to player
                                SKSE::log::info("Height diff: {:.1f} - moving target up from {:.1f} to {:.1f}",
                                    heightDiff, targetPos.z, higherZ);
                                feedTarget->SetPosition(RE::NiPoint3(targetPos.x, targetPos.y, higherZ), true);
                            }
                        } else if (heightDiff > settings->NonCombat.MaxHeightDiff) {
                            SKSE::log::warn("Height diff {:.1f} exceeds max {:.1f} - skipping repositioning",
                                heightDiff, settings->NonCombat.MaxHeightDiff);
                        }
                    }
                }

                // Get vampire hunger stage and calculate/select feed type
                auto* settings = Settings::GetSingleton();
                int vampireStage = PapyrusCall::GetVampireStage();
                int feedType;

                if (settings->General.ForceFeedType > 0) {
                    // Debug override
                    feedType = settings->General.ForceFeedType;
                    SKSE::log::info("Using FORCED FeedType: {}", feedType);
                } else if (targetState == FeedState::kSleeping) {
                    // Sleeping targets: skip SelectAnimation (which rotates target)
                    // Bed/bedroll uses left/right based on player position, not front/back
                    feedType = FeedState::Calculate(targetState, vampireStage);
                    SKSE::log::info("Sleeping target - skipping rotation. FeedType: {}", feedType);
                } else {
                    // Standing/sitting/combat: use SelectAnimation (may rotate target)
                    feedType = FeedState::SelectAnimation(isInCombat, vampireStage, feedTarget);

                    if (feedType == 0) {
                        // No animations configured, use default calculation
                        feedType = FeedState::Calculate(targetState, vampireStage);
                        SKSE::log::info("Vampire stage: {} | FeedType: {} (state={} + stage={})",
                            vampireStage, feedType, targetState, vampireStage);
                    }
                }

                // Set graph variables on both player and target for OAR conditions
                // These are read by OAR's HasGraphVariable condition
                // Only works if user has Behavior Data Injector installed
                SetFeedGraphVars(player, feedType);
                SetFeedGraphVars(feedTarget, feedType);

                // Select idle based on target state and position
                // isBehind was already computed by SelectAnimation() via RotateTargetToClosest()
                const char* idleEditorID = nullptr;

                // Determine if this is a paired animation or solo idle
                bool isPairedAnim = true;

                if (targetState == FeedState::kSleeping && furnitureRef) {
                    // Bed/bedroll - NOT paired, solo idle on player
                    isPairedAnim = false;
                    bool isLeft = CustomFeed::IsPlayerOnLeftSide(feedTarget);
                    bool isBedroll = CustomFeed::IsBedroll(furnitureRef);
                    if (isBedroll) {
                        idleEditorID = isLeft ? CustomFeed::Idles::BEDROLL_LEFT : CustomFeed::Idles::BEDROLL_RIGHT;
                        SKSE::log::info("Bedroll {} side (solo idle)", isLeft ? "left" : "right");
                    } else {
                        idleEditorID = isLeft ? CustomFeed::Idles::BED_LEFT : CustomFeed::Idles::BED_RIGHT;
                        SKSE::log::info("Bed {} side (solo idle)", isLeft ? "left" : "right");
                    }
                } else if (targetState == FeedState::kSitting) {
                    // Sitting - front/back already determined by SelectAnimation
                    // Note: SelectAnimation calls RotateTargetToClosest which returns isBehind
                    // We need to check again since feedType doesn't encode front/back directly
                    bool isBehind = FeedState::RotateTargetToClosest(feedTarget);
                    idleEditorID = isBehind ? CustomFeed::Idles::SITTING_BACK : CustomFeed::Idles::SITTING_FRONT;
                    SKSE::log::info("Sitting {} feed", isBehind ? "back" : "front");
                } else if (targetState == FeedState::kCombat) {
                    // Combat - front/back
                    bool isBehind = FeedState::RotateTargetToClosest(feedTarget);
                    idleEditorID = isBehind ? CustomFeed::Idles::COMBAT_BACK : CustomFeed::Idles::COMBAT_FRONT;
                    SKSE::log::info("Combat {} feed", isBehind ? "back" : "front");
                } else {
                    // Standing - front/back
                    bool isBehind = FeedState::RotateTargetToClosest(feedTarget);
                    idleEditorID = isBehind ? CustomFeed::Idles::STANDING_BACK : CustomFeed::Idles::STANDING_FRONT;
                    SKSE::log::info("Standing {} feed", isBehind ? "back" : "front");
                }

                // Play the animation (paired or solo)
                SKSE::log::info("Calling CustomFeed::PlayPairedFeed with idle '{}' (paired={})...", idleEditorID, isPairedAnim);
                if (CustomFeed::PlayPairedFeed(idleEditorID, feedTarget, isPairedAnim)) {
                    // Send OnVampireFeed event to target (same as vanilla StartVampireFeed)
                    PapyrusCall::SendOnVampireFeedEvent(feedTarget);

                    // Call the Papyrus VampireFeed() function to update vampire status
                    auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
                    if (vampireQuest) {
                        PapyrusCall::CallVampireFeed(vampireQuest, feedTarget);
                    } else {
                        SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
                    }
                } else {
                    SKSE::log::warn("CustomFeed failed");
                    // TODO: Re-enable vanilla fallback after testing
                    // player->InitiateVampireFeedPackage(feedTarget, furnitureRef);
                }
            }
            break;
        case SkyPromptAPI::PromptEventType::kDeclined:
            SKSE::log::debug("Feed DECLINED");
            break;
        case SkyPromptAPI::PromptEventType::kDown:
            SKSE::log::debug("Feed button DOWN");
            break;
        case SkyPromptAPI::PromptEventType::kUp:
            SKSE::log::debug("Feed button UP");
            break;
        case SkyPromptAPI::PromptEventType::kTimingOut:
            // Prompt is about to expire - resend to refresh it (but not during animation)
            if (currentTarget_ && g_clientID != 0) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                bool bIsSynced = player && PairedAnimation::IsInPairedAnimation(player);

                SKSE::log::debug("Prompt timing out - bIsSynced: {}", bIsSynced);

                // Only refresh prompt if not in animation
                if (!bIsSynced) {
                    [[maybe_unused]] bool sent = SkyPromptAPI::SendPrompt(this, g_clientID);
                    SKSE::log::debug("Prompt timing out - refreshed (sent={})", sent);
                }
            }
            break;
        default:
            break;
        }
    }

    void SetTarget(RE::Actor* target) {
        currentTarget_ = target;
        if (target) {
            prompts_[0] = SkyPromptAPI::Prompt(
                "Feed",
                1,  // eventID
                1,  // actionID
                SkyPromptAPI::PromptType::kSinglePress,
                target->GetFormID(),
                feedButtons_
            );
        }
    }

    RE::Actor* GetTarget() const {
        return currentTarget_;
    }

    // Set graph variables for OAR animation conditions
    // These only work if user has Behavior Data Injector installed
    // If not installed, SetGraphVariable silently fails - safe to call always
    static void SetFeedGraphVars(RE::Actor* actor, int feedType) {
        if (!actor) return;

        // Set bool flag
        bool success = actor->SetGraphVariableBool(GraphVars::IsSkyPromptFeeding, true);
        if (success) {
            SKSE::log::debug("Set graph var {} = true on {}",
                GraphVars::IsSkyPromptFeeding, actor->GetName());
        }

        // Set composite feed type (TargetState * 10 + HungerStage)
        success = actor->SetGraphVariableInt(GraphVars::SkyPromptFeedType, feedType);
        if (success) {
            SKSE::log::debug("Set graph var {} = {} on {}",
                GraphVars::SkyPromptFeedType, feedType, actor->GetName());
        }
    }

    static void ClearFeedGraphVars(RE::Actor* actor) {
        if (!actor) return;

        actor->SetGraphVariableBool(GraphVars::IsSkyPromptFeeding, false);
        actor->SetGraphVariableInt(GraphVars::SkyPromptFeedType, 0);
        SKSE::log::debug("Cleared feed graph vars on {}", actor->GetName());
    }

    // Returns true if target should be excluded from feeding (no prompt shown)
    static bool IsExcluded(RE::Actor* actor) {
        if (!actor) return true;

        auto* settings = Settings::GetSingleton();
        if (!settings->General.EnableMod) return true;

        // Check if player or target is in a paired animation
        // Skip this check while our feed is active (PairEnd will handle prompt refresh)
        if (!FeedAnimState::IsFeedActive()) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && PairedAnimation::IsInPairedAnimation(player)) {
                SKSE::log::debug("Excluded: player is in paired animation");
                return true;
            }
            if (PairedAnimation::IsInPairedAnimation(actor)) {
                SKSE::log::debug("Excluded: {} is in paired animation", actor->GetName());
                return true;
            }
        }

        // Check common filters first (level, keywords)
        if (FeedFiltering::IsExcludedByFilters(actor)) return true;

        bool isInCombat = actor->IsInCombat();

        SKSE::log::debug("IsExcluded check: {} | InCombat: {}", actor->GetName(), isInCombat);

        if (isInCombat) {
            return FeedFiltering::IsExcludedCombat(actor);
        } else {
            return FeedFiltering::IsExcludedNonCombat(actor);
        }
    }

private:
    VampireFeedSink() {
        prompts_[0] = SkyPromptAPI::Prompt(
            "Feed",
            1,  // eventID
            1,  // actionID
            SkyPromptAPI::PromptType::kSinglePress,
            0,
            feedButtons_
        );
    }

    // G key (0x22) + Gamepad A (0x1000)
    static constexpr std::array<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>, 2> feedButtons_ = {{
        {RE::INPUT_DEVICE::kKeyboard, 0x22},
        {RE::INPUT_DEVICE::kGamepad, 0x1000}
    }};

    std::array<SkyPromptAPI::Prompt, 1> prompts_;
    mutable RE::Actor* currentTarget_ = nullptr;
};

// Refresh prompt after animation ends - called directly from animation event
// since crosshair event won't fire if still looking at same target
inline void RefreshFeedPromptAfterAnimation() {
    if (g_clientID == 0) return;

    auto* sink = VampireFeedSink::GetSingleton();
    auto* target = sink->GetTarget();

    if (target) {
        // Re-send the prompt for the current target
        bool sent = SkyPromptAPI::SendPrompt(sink, g_clientID);
        SKSE::log::info("Refreshed prompt after animation for: {} - sent: {}",
            target->GetName(), sent);
    }
}
