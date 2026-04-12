#pragma once
#include "SkyPrompt/API.hpp"
#include <mutex>
#include <atomic>
#include <chrono>
#include <array>
#include <span>
#include <vector>
#include <functional>

// Forward declarations
class Settings;

/// Definition of a prompt that can be shown to the player
struct PromptDef {
    std::string text;
    SkyPromptAPI::PromptType type = SkyPromptAPI::PromptType::kSinglePress;
    float holdDuration = 0.0f;
    uint32_t color = 0xFFFFFFFF;  // AABBGGRR
    int priority = 100;           // Higher = primary button
    std::function<void(RE::Actor* target, bool holdComplete)> onAccept;
};

// NOTE: FeedAnimState namespace removed - use FeedSession instead
// #include "feed/FeedSession.h" for FeedSession::IsActive(), etc.

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
    /// Callback type for prompt providers
    using PromptCallback = std::function<std::vector<PromptDef>(RE::Actor* target)>;

    static PairedAnimPromptSink* GetSingleton();

    std::span<const SkyPromptAPI::Prompt> GetPrompts() const override;
    void ProcessEvent(SkyPromptAPI::PromptEvent event) const override;

    void SetTarget(RE::Actor* target);
    RE::NiPointer<RE::Actor> GetTarget() const;

    static bool IsExcluded(RE::Actor* actor);
    static bool IsValidFeedTarget(RE::Actor* target);

    /// Register a callback that provides prompts for a target
    void RegisterPromptCallback(PromptCallback callback);

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

    // Called when feed prompt is accepted (used by callbacks)
    void HandleFeedAccepted();

    // Lethal feed state (set by callbacks before HandleFeedAccepted)
    mutable bool isLethalFeedInProgress_{false};

    // Embrace feed state (set by Sacrosanct callback before HandleFeedAccepted)
    mutable bool isEmbraceFeedInProgress_{false};

private:
    PairedAnimPromptSink();

    void RegisterCorePromptCallback();

    // NOTE: ExecuteFeed removed - replaced by FeedSession::Start()

    void HandleFeedAcceptedTest();  // Minimal test for kill move playback
    void HandleTimingOut();

    // Thread-safe wrapper methods for currentTargetHandle_
    void SetTargetHandle(const RE::ObjectRefHandle& handle);
    RE::ObjectRefHandle GetTargetHandle() const;

    void ShowPrompt(RE::Actor* target);
    void HidePrompt();

    // Button bindings per prompt slot (primary, secondary)
    std::array<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>, 2> feedButtons_;
    std::array<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>, 2> secondaryButtons_;

    // Prompt callbacks from integrations
    std::vector<PromptCallback> promptCallbacks_;

    // Current prompt definitions and API prompts
    mutable std::vector<PromptDef> currentPromptDefs_;
    mutable std::vector<SkyPromptAPI::Prompt> prompts_;

    mutable RE::ObjectRefHandle currentTargetHandle_;
    mutable RE::ObjectRefHandle activeFeedTargetHandle_;
    mutable std::mutex targetMutex_;  // Protects currentTargetHandle_ and activeFeedTargetHandle_
    RE::ObjectRefHandle lastCrosshairActor_;

    // Prompt delay tracking - wait before showing prompt on new target
    RE::ObjectRefHandle pendingTarget_;
    std::chrono::steady_clock::time_point pendingTargetTime_;
};
