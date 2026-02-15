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

private:
    PairedAnimPromptSink();

    static void ExecuteFeed(const char* idleEditorID, RE::Actor* target, bool isPairedAnim, bool isLethal = false, bool hasOARAnimation = false);

    void HandleFeedAccepted() const;
    void HandleTimingOut() const;

    // Thread-safe wrapper methods for currentTargetHandle_
    void SetTargetHandle(const RE::ObjectRefHandle& handle);
    RE::ObjectRefHandle GetTargetHandle() const;

    void ShowPrompt(RE::Actor* target);
    void HidePrompt();

    std::array<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>, 2> feedButtons_;

    std::array<SkyPromptAPI::Prompt, 1> prompts_;
    mutable RE::ObjectRefHandle currentTargetHandle_;
    mutable std::mutex targetMutex_;
    RE::ObjectRefHandle lastCrosshairActor_;
    mutable bool isLethalFeedInProgress_{false};  // Mutable: transient state for event processing
};
