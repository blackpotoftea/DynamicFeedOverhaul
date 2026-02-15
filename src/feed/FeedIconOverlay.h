#pragma once
#include <chrono>
#include <atomic>
#include <string>
#include <mutex>
#include "RE/Skyrim.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

class FeedIconOverlay {
public:
    enum class IconPosition {
        AboveHead,      // Icon positioned above NPC's head
        RightOfHead     // Icon positioned to the right of NPC's head
    };

    struct OverlayState {
        RE::ActorHandle target;
        std::string currentIconPath; // Changed to std::string
        std::chrono::steady_clock::time_point startTime;
        float duration{3.0f};  // Display for 3 seconds
        std::atomic<bool> active{false};
        std::atomic<bool> feeding{false}; // New state for "bite" animation
        std::chrono::steady_clock::time_point feedStartTime; // When feeding started
        ImGuiMCP::ImVec2 lastScreenPos{0.0f, 0.0f};  // For smoothing
        bool hasLastPos{false};

        bool IsExpired() const {
            if (!active.load()) return true;
            if (feeding.load()) return false; // Don't expire during feed animation
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
            return elapsed >= duration;
        }
    };

    // Singleton access
    static FeedIconOverlay* GetSingleton();

    // Start displaying icon above target's head
    void ShowIcon(RE::Actor* target, const std::string& iconPath, float durationSeconds = 3.0f);

    // Trigger the "bite" animation (red tint + squeeze/shut) and then hide
    void TriggerFeedAnimation();

    // Render the icon overlay (called from render hook)
    void RenderOverlay();

    // Stop displaying icon
    void StopIcon();

    // Set/Get icon position mode
    void SetIconPosition(IconPosition position);
    IconPosition GetIconPosition() const;

private:
    FeedIconOverlay() = default;
    ~FeedIconOverlay() = default;
    FeedIconOverlay(const FeedIconOverlay&) = delete;
    FeedIconOverlay& operator=(const FeedIconOverlay&) = delete;

    // Get attached object's screen position using multi-strategy positioning
    ImGuiMCP::ImVec2 GetAttachedObjectPos(RE::Actor* target);

    // Convert world position to screen position (with camera parameter - for UI3D)
    ImGuiMCP::ImVec2 WorldToScreenLoc(const RE::NiPoint3& position, const RE::NiPointer<RE::NiCamera>& a_cam);

    // Convert world position to screen position (using global camera matrix - for game world)
    ImGuiMCP::ImVec2 WorldToScreenLoc(const RE::NiPoint3& position);

    // Clamp screen position with overshoot tolerance
    void FastClampToScreen(ImGuiMCP::ImVec2& point);

    // Calculate position offset to the right of an object relative to camera
    void OffsetRight(const RE::NiPoint3& pos, const RE::NiPoint3& camPos, RE::NiPoint3& out, float offset);

    std::mutex _mutex;
    OverlayState _state;
    IconPosition _iconPosition{IconPosition::AboveHead};
};
