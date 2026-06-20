#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include "RE/Skyrim.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

// Custom ImGui health bar drawn above the victim's head for the duration of a
// feed. Reliable replacement for the TrueHUD bar (which TrueHUD hides for a
// non-target NPC out of combat). Rendered as a HUD element registered in
// plugin.cpp, identical to FeedIconOverlay.
class FeedHealthBarOverlay {
public:
    static FeedHealthBarOverlay* GetSingleton();

    // Start showing the bar for `target` (persists until Hide()).
    void Show(RE::Actor* target);

    // Stop showing the bar (idempotent).
    void Hide();

    // Draw the bar; called every frame from the HUD render hook.
    void RenderOverlay();

private:
    FeedHealthBarOverlay() = default;
    ~FeedHealthBarOverlay() = default;
    FeedHealthBarOverlay(const FeedHealthBarOverlay&) = delete;
    FeedHealthBarOverlay& operator=(const FeedHealthBarOverlay&) = delete;

    // Project a world position to screen space. Returns false if behind camera.
    bool WorldToScreen(const RE::NiPoint3& world, ImGuiMCP::ImVec2& outScreen);

    std::mutex _mutex;
    std::atomic<bool> _active{ false };
    RE::ActorHandle _target;
    ImGuiMCP::ImVec2 _lastScreenPos{ 0.0f, 0.0f };
    bool _hasLastPos{ false };

    // Trailing "damage chip" layer state.
    float _trailingPct{ 1.0f };   // lagging display value that catches up to the front bar
    float _lastPct{ 1.0f };       // previous frame's actual health fraction
    bool _hasTrail{ false };      // false until first render seeds the values
    std::chrono::steady_clock::time_point _lastFrameTime;
    std::chrono::steady_clock::time_point _lastDrainTime;  // when health last dropped
};
