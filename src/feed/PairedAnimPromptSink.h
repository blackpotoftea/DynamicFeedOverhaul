#pragma once
#include "SkyPrompt/API.hpp"
#include <mutex>
#include <atomic>
#include <chrono>
#include <array>
#include <span>

// Forward declarations
class Settings;

namespace FeedAnimState {
    void MarkFeedStarted();
    void MarkFeedEnded();
    bool CheckAndClearFeedEnded();
    bool IsFeedActive();
}

class AnimEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
    static AnimEventSink* GetSingleton();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::BSAnimationGraphEvent* event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) override;

    static void Register();
    static void Unregister();
    static void CheckTimeout();

private:
    AnimEventSink() = default;
    static std::chrono::steady_clock::time_point registeredTime_;
    static std::mutex mutex_;
};

class PairedAnimPromptSink : public SkyPromptAPI::PromptSink {
public:
    static PairedAnimPromptSink* GetSingleton();

    std::span<const SkyPromptAPI::Prompt> GetPrompts() const override;
    void ProcessEvent(SkyPromptAPI::PromptEvent event) const override;

    void SetTarget(RE::Actor* target);
    RE::NiPointer<RE::Actor> GetTarget() const;

    static bool IsExcluded(RE::Actor* actor);
    static bool IsValidFeedTarget(RE::Actor* target);
    
    // Helper to update buttons from settings
    void UpdateFeedButtons();

    // Event handlers for game state changes
    void OnCrosshairUpdate(RE::Actor* newTarget);
    void OnMenuStateChange(bool isMenuOpen);
    void OnPeriodicValidation();
    void RefreshPrompt();

    // Target being fed on during active feed (accessed by witness detection hook)
    // Thread-safe access via GetActiveFeedTarget() / SetActiveFeedTarget()
    RE::NiPointer<RE::Actor> GetActiveFeedTarget() const;
    void SetActiveFeedTarget(RE::Actor* target);

    // Timers for periodic checks (updated from PlayerUpdateHook)
    float periodicCheckTimer_ = 0.0f;
    float witnessCheckTimer_ = 0.0f;

    // Reset timers (call on loading screens or game state changes if needed)
    void ResetTimers() {
        periodicCheckTimer_ = 0.0f;
        witnessCheckTimer_ = 0.0f;
    }

private:
    PairedAnimPromptSink();

    static void ExecuteFeed(const char* idleEditorID, RE::Actor* target, bool isPairedAnim, bool isLethal = false, bool hasOARAnimation = false);

    void HandleFeedAccepted();
    void HandleTimingOut();

    // Thread-safe wrapper methods for currentTargetHandle_
    void SetTargetHandle(const RE::ObjectRefHandle& handle);
    RE::ObjectRefHandle GetTargetHandle() const;

    void ShowPrompt(RE::Actor* target);
    void HidePrompt();

    std::array<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>, 2> feedButtons_;

    mutable std::array<SkyPromptAPI::Prompt, 1> prompts_;  // Mutable: modified in const SetTarget during ProcessEvent
    mutable RE::ObjectRefHandle currentTargetHandle_;
    mutable RE::ObjectRefHandle activeFeedTargetHandle_;
    mutable std::mutex targetMutex_;  // Protects currentTargetHandle_ and activeFeedTargetHandle_
    RE::ObjectRefHandle lastCrosshairActor_;
    mutable bool isLethalFeedInProgress_{false};  // Mutable: transient state for event processing

    // Prompt delay tracking - wait before showing prompt on new target
    RE::ObjectRefHandle pendingTarget_;
    std::chrono::steady_clock::time_point pendingTargetTime_;
};
