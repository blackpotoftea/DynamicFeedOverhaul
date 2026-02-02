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
            HandleFeedAccepted();
            break;
        case SkyPromptAPI::PromptEventType::kTimingOut:
            HandleTimingOut();
            break;
        case SkyPromptAPI::PromptEventType::kDeclined:
        case SkyPromptAPI::PromptEventType::kDown:
        case SkyPromptAPI::PromptEventType::kUp:
        default:
            break;
        }
    }

private:
    // Determine target state for feed type calculation
    static int DetermineTargetState(RE::Actor* target, bool& outIsInCombat) {
        outIsInCombat = target->IsInCombat();

        if (outIsInCombat) {
            return FeedState::kCombat;
        } else if (TargetState::IsSleeping(target)) {
            return FeedState::kSleeping;
        } else if (TargetState::IsSitting(target)) {
            return FeedState::kSitting;
        }
        return FeedState::kStanding;
    }

    // Apply height adjustment for standing feeds on stairs
    static void ApplyHeightAdjustment(RE::PlayerCharacter* player, RE::Actor* target, const Settings* settings) {
        if (!settings->NonCombat.EnableHeightAdjust) return;

        auto playerPos = player->GetPosition();
        auto targetPos = target->GetPosition();
        float heightDiff = std::fabs(targetPos.z - playerPos.z);

        if (heightDiff <= settings->NonCombat.MinHeightDiff) return;

        if (heightDiff > settings->NonCombat.MaxHeightDiff) {
            SKSE::log::warn("Height diff {:.1f} exceeds max {:.1f} - skipping repositioning",
                heightDiff, settings->NonCombat.MaxHeightDiff);
            return;
        }

        float higherZ = std::max(playerPos.z, targetPos.z);
        if (playerPos.z < targetPos.z) {
            SKSE::log::debug("Height diff: {:.1f} - moving player up", heightDiff);
            player->SetPosition(RE::NiPoint3(playerPos.x, playerPos.y, higherZ), true);
        } else {
            SKSE::log::debug("Height diff: {:.1f} - moving target up", heightDiff);
            target->SetPosition(RE::NiPoint3(targetPos.x, targetPos.y, higherZ), true);
        }
    }

    // Select idle animation based on target state and position
    // Returns the idle editor ID and whether it's a paired animation
    static const char* SelectIdleAnimation(int targetState, RE::Actor* target,
                                           RE::TESObjectREFR* furnitureRef, bool isBehind,
                                           bool& outIsPairedAnim) {
        outIsPairedAnim = true;

        if (targetState == FeedState::kSleeping && furnitureRef) {
            outIsPairedAnim = false;
            bool isLeft = CustomFeed::IsPlayerOnLeftSide(target);
            bool isBedroll = CustomFeed::IsBedroll(furnitureRef);

            if (isBedroll) {
                SKSE::log::debug("Bedroll {} side (solo idle)", isLeft ? "left" : "right");
                return isLeft ? CustomFeed::Idles::BEDROLL_LEFT : CustomFeed::Idles::BEDROLL_RIGHT;
            } else {
                SKSE::log::debug("Bed {} side (solo idle)", isLeft ? "left" : "right");
                return isLeft ? CustomFeed::Idles::BED_LEFT : CustomFeed::Idles::BED_RIGHT;
            }
        }

        const char* posStr = isBehind ? "back" : "front";

        if (targetState == FeedState::kSitting) {
            SKSE::log::debug("Sitting {} feed", posStr);
            return isBehind ? CustomFeed::Idles::SITTING_BACK : CustomFeed::Idles::SITTING_FRONT;
        } else {
            // Standing and combat use same base idle - OAR uses feedType to select custom animations
            SKSE::log::debug("{} {} feed", targetState == FeedState::kCombat ? "Combat" : "Standing", posStr);
            return isBehind ? CustomFeed::Idles::STANDING_BACK : CustomFeed::Idles::STANDING_FRONT;
        }
    }

    // Execute the feed animation and update vampire status
    static void ExecuteFeed(const char* idleEditorID, RE::Actor* target, bool isPairedAnim) {
        SKSE::log::info("Playing feed idle '{}' (paired={})", idleEditorID, isPairedAnim);

        if (CustomFeed::PlayPairedFeed(idleEditorID, target, isPairedAnim)) {
            PapyrusCall::SendOnVampireFeedEvent(target);

            auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
            if (vampireQuest) {
                PapyrusCall::CallVampireFeed(vampireQuest, target);
            } else {
                SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
            }
        } else {
            SKSE::log::warn("CustomFeed failed");
        }
    }

    void HandleFeedAccepted() const {
        if (!currentTarget_) return;

        SkyPromptAPI::RemovePrompt(this, g_clientID);
        FeedAnimState::MarkFeedStarted();
        AnimEventSink::Register();

        RE::Actor* feedTarget = currentTarget_;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto* settings = Settings::GetSingleton();
        auto furnitureRef = TargetState::GetFurnitureReference(feedTarget);

        SKSE::log::info("Feed ACCEPTED on target: {} (FormID: {:X})",
            feedTarget->GetName(), feedTarget->GetFormID());

        // Determine target state
        bool isInCombat = false;
        int targetState = DetermineTargetState(feedTarget, isInCombat);
        SKSE::log::debug("Target state: {} (combat={})", targetState, isInCombat);

        // Apply height adjustment for standing targets
        if (targetState == FeedState::kStanding) {
            ApplyHeightAdjustment(player, feedTarget, settings);
        }

        // Calculate feed type and determine position (front/back)
        int vampireStage = PapyrusCall::GetVampireStage();
        auto [feedType, isBehind] = FeedState::DetermineFeedTypeAndPosition(
            targetState, vampireStage, isInCombat, feedTarget,
            settings->General.ForceFeedType);

        // Set graph variables for OAR conditions
        SetFeedGraphVars(player, feedType);
        SetFeedGraphVars(feedTarget, feedType);

        // Select and play animation
        bool isPairedAnim = true;
        const char* idleEditorID = SelectIdleAnimation(targetState, feedTarget, furnitureRef, isBehind, isPairedAnim);
        ExecuteFeed(idleEditorID, feedTarget, isPairedAnim);
    }

    void HandleTimingOut() const {
        if (!currentTarget_ || g_clientID == 0) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && PairedAnimation::IsInPairedAnimation(player)) {
            SKSE::log::debug("Prompt timing out - skipped (in animation)");
            return;
        }

        [[maybe_unused]] bool sent = SkyPromptAPI::SendPrompt(this, g_clientID);
        SKSE::log::debug("Prompt timing out - refreshed (sent={})", sent);
    }

public:

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
