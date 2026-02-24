#include "feed/FeedIconOverlay.h"
#include <filesystem>
#include "Settings.h"

namespace {
    constexpr float CLAMP_MAX_OVERSHOOT = 100.0f;
    constexpr float PADDING = 10.0f;
    // constexpr float Z_OFFSET = 20.0f; // Now in Settings
    constexpr float SMOOTH_FACTOR = 0.3f;
    constexpr float NPC_HEAD_SIZE = 15.0f;
}

// Animation result structure
struct AnimationResult {
    float widthScale = 1.0f;
    float heightScale = 1.0f;
    float sizeScale = 1.0f;
    ImGuiMCP::ImU32 tintColor = IM_COL32(255, 255, 255, 255);
    bool isComplete = false;
};

// Image animation functions
namespace ImageAnimation {
    AnimationResult CalculateBiteAnimation(float elapsedSeconds, float animDuration = 0.6f);
    AnimationResult CalculateOverlayImgAnimation(float elapsedSeconds, float totalDuration);
}

AnimationResult ImageAnimation::CalculateBiteAnimation(float feedElapsed, float animDuration) {
    AnimationResult result;

    if (feedElapsed >= animDuration) {
        result.isComplete = true;
        return result;
    }

    // 1. Red Tint (Transition from White to Red)
    // Lerp from White (255,255,255) to Red (255,0,0) very quickly
    float redFactor = std::min(1.0f, feedElapsed * 6.0f);
    result.tintColor = IM_COL32(255,
                                static_cast<int>(255 * (1.0f - redFactor)),
                                static_cast<int>(255 * (1.0f - redFactor)),
                                255);

    // 2. Pulse/Bite Effect (Faster timing)
    if (feedElapsed < 0.1f) {
        // Phase 1: Anticipation (0.0s - 0.1s)
        float t = feedElapsed / 0.1f;
        float s = 1.0f + 0.1f * t;
        result.widthScale = s;
        result.heightScale = s;
    } else if (feedElapsed < 0.2f) {
        // Phase 2: Bite Shut (0.1s - 0.2s) - FAST SNAP
        float t = (feedElapsed - 0.1f) / 0.1f;
        result.heightScale = 1.1f + (0.5f - 1.1f) * t;
        result.widthScale = 1.1f + (1.2f - 1.1f) * t;
    } else if (feedElapsed < 0.3f) {
        // Phase 3: Bounce Back (0.2s - 0.3f) - FAST RECOIL
        float t = (feedElapsed - 0.2f) / 0.1f;
        result.heightScale = 0.5f + (1.0f - 0.5f) * t;
        result.widthScale = 1.2f + (1.0f - 1.2f) * t;
    } else {
        // Phase 4: Hold (0.3s - 0.6s)
        result.heightScale = 1.0f;
        result.widthScale = 1.0f;

        // Fade out alpha in the last 0.15s
        if (feedElapsed > 0.45f) {
            float fadeT = (feedElapsed - 0.45f) / 0.15f;
            float alpha = 1.0f - fadeT;
            result.tintColor = (result.tintColor & 0x00FFFFFF) | (static_cast<int>(255 * alpha) << 24);
        }
    }

    return result;
}

AnimationResult ImageAnimation::CalculateOverlayImgAnimation(float totalElapsed, float totalDuration) {
    AnimationResult result;

    // Gentle Pulse applied to base size
    result.sizeScale = 1.0f + 0.05f * sin(totalElapsed * 2.0f);

    // Fade out near end
    float timeRemaining = totalDuration - totalElapsed;
    if (timeRemaining < 0.5f) {
        float alpha = std::max(0.0f, timeRemaining / 0.5f);
        result.tintColor = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));
    }

    return result;
}

FeedIconOverlay* FeedIconOverlay::GetSingleton() {
    static FeedIconOverlay singleton;
    return &singleton;
}

void FeedIconOverlay::SetIconPosition(IconPosition position) {
    std::lock_guard<std::mutex> lock(_mutex);
    _iconPosition = position;
    SKSE::log::info("Icon position mode set to: {}",
        position == IconPosition::AboveHead ? "AboveHead" : "RightOfHead");
}

FeedIconOverlay::IconPosition FeedIconOverlay::GetIconPosition() const {
    return _iconPosition;
}

void FeedIconOverlay::OffsetRight(const RE::NiPoint3& a_pos, const RE::NiPoint3& a_cam_pos, RE::NiPoint3& a_out,
                    const float a_offset) {
    const auto diff = a_pos - a_cam_pos;
    constexpr RE::NiPoint3 z_vec(0.f, 0.f, 1.f);
    const auto right_vec = diff.UnitCross(z_vec);
    a_out = a_pos + right_vec * a_offset;
}

void FeedIconOverlay::FastClampToScreen(ImGuiMCP::ImVec2& point) {
    const ImGuiMCP::ImVec2 rect = ImGuiMCP::GetIO()->DisplaySize;
    if (point.x < 0.0f) {
        const float overshootX = abs(point.x);
        if (overshootX > CLAMP_MAX_OVERSHOOT) point.x += overshootX - CLAMP_MAX_OVERSHOOT;
    } else if (point.x > rect.x) {
        const float overshootX = point.x - rect.x;
        if (overshootX > CLAMP_MAX_OVERSHOOT) point.x -= overshootX - CLAMP_MAX_OVERSHOOT;
    }

    if (point.y < 0.0f) {
        const float overshootY = abs(point.y);
        if (overshootY > CLAMP_MAX_OVERSHOOT) point.y += overshootY - CLAMP_MAX_OVERSHOOT;
    } else if (point.y > rect.y) {
        const float overshootY = point.y - rect.y;
        if (overshootY > CLAMP_MAX_OVERSHOOT) point.y -= overshootY - CLAMP_MAX_OVERSHOOT;
    }
}

// WorldToScreenLoc with camera parameter (for UI3D camera with inventory items)
ImGuiMCP::ImVec2 FeedIconOverlay::WorldToScreenLoc(const RE::NiPoint3& position, const RE::NiPointer<RE::NiCamera>& a_cam) {
    float z;
    ImGuiMCP::ImVec2 screenLocOut;
    RE::NiCamera::WorldPtToScreenPt3(a_cam->GetRuntimeData().worldToCam,
                                        a_cam->GetRuntimeData2().port,
                                        position, screenLocOut.x, screenLocOut.y, z, 1e-5f);
    const ImGuiMCP::ImVec2 rect = ImGuiMCP::GetIO()->DisplaySize;
    screenLocOut.x = rect.x * screenLocOut.x;
    screenLocOut.y = 1.0f - screenLocOut.y;  // Y-axis flip for ImGui
    screenLocOut.y = rect.y * screenLocOut.y;
    return screenLocOut;
}

// WorldToScreenLoc using global world-to-camera matrix (for game world NPCs)
ImGuiMCP::ImVec2 FeedIconOverlay::WorldToScreenLoc(const RE::NiPoint3& position) {
    static uintptr_t g_worldToCamMatrix = RELOCATION_ID(519579, 406126).address(); // 2F4C910, 2FE75F0
    static auto g_viewPort = (RE::NiRect<float>*)RELOCATION_ID(519618, 406160).address(); // 2F4DED0, 2FE8B98

    ImGuiMCP::ImVec2 screenLocOut;
    float zVal;

    RE::NiCamera::WorldPtToScreenPt3((float(*)[4])g_worldToCamMatrix, *g_viewPort, position,
                                        screenLocOut.x, screenLocOut.y, zVal, 1e-5f);
    const ImGuiMCP::ImVec2 rect = ImGuiMCP::GetIO()->DisplaySize;

    screenLocOut.x = rect.x * screenLocOut.x;
    screenLocOut.y = 1.0f - screenLocOut.y;  // Y-axis flip for ImGui
    screenLocOut.y = rect.y * screenLocOut.y;

    return screenLocOut;
}

ImGuiMCP::ImVec2 FeedIconOverlay::GetAttachedObjectPos(RE::Actor* target) {
    if (!target) {
        SKSE::log::warn("GetAttachedObjectPos: target is null");
        return ImGuiMCP::ImVec2{};
    }

    auto settings = Settings::GetSingleton();
    float iconSize = settings->IconOverlay.IconSize;

    // SKSE::log::trace("GetAttachedObjectPos called for: {}", target->GetName());

    RE::NiPoint3 pos;
    ImGuiMCP::ImVec2 pos2d;

    // Strategy A: Check if target is in Inventory 3D Manager (inventory preview)
    if (const auto inv3dmngr = RE::Inventory3DManager::GetSingleton()) {
        if (const auto temp_ref = inv3dmngr->tempRef;
            temp_ref && target->GetFormID() == temp_ref->GetFormID()) {
            if (!inv3dmngr->GetRuntimeData().loadedModels.empty()) {
                if (const auto& model = inv3dmngr->GetRuntimeData().loadedModels.back().spModel) {
                    // Position to the right of the inventory item
                    OffsetRight(model->world.translate,
                                RE::UI3DSceneManager::GetSingleton()->cachedCameraPos,
                                pos,
                                model->worldBound.radius);
                    pos2d = WorldToScreenLoc(pos, RE::UI3DSceneManager::GetSingleton()->camera);
                    pos2d.x += (iconSize + PADDING);
                    pos2d.y += 0;

                    FastClampToScreen(pos2d);
                    return pos2d;
                }
            }
        }
    }

    // Strategy B: NPC Head Node (primary strategy for actors)
    
    if (const auto a_head = [&]() -> RE::NiAVObject* {
        if (const auto actor = target->As<RE::Actor>()) {
            if (const auto middle = actor->GetMiddleHighProcess()) {
                if (middle->headNode) {
                    return middle->headNode;
                }
            }
        }
        return nullptr;
    }()) {
        const float objectScale = target->GetScale();
        const auto npc_head_pos = a_head->world.translate;

        // Position icon based on current mode
        if (_iconPosition == IconPosition::AboveHead) {
            // Position above the head by adding Z offset (scaled by NPC size)
            pos = npc_head_pos + RE::NiPoint3(0.f, 0.f, settings->IconOverlay.IconHeightOffset * objectScale);

            // Use global camera matrix for game world NPCs
            pos2d = WorldToScreenLoc(pos);

            // Offset upward in screen space
            pos2d.y -= (iconSize + PADDING);

        } else {  // IconPosition::RightOfHead
            // Position to the right of the head
            const auto cameraPos = RE::PlayerCamera::GetSingleton()->GetRuntimeData2().pos;
            const auto diff = npc_head_pos - cameraPos;
            constexpr RE::NiPoint3 z_vec(0.f, 0.f, 1.f);
            const auto right_vec = diff.UnitCross(z_vec);
            pos = npc_head_pos + right_vec * (NPC_HEAD_SIZE * objectScale);

            // Use global camera matrix for game world NPCs
            pos2d = WorldToScreenLoc(pos);

            // Offset to the right in screen space
            pos2d.x += (iconSize + PADDING);
        }

        FastClampToScreen(pos2d);
        return pos2d;
    }

    // No valid strategy found
    // SKSE::log::warn("No valid positioning strategy found - returning (0, 0)");
    return ImGuiMCP::ImVec2{};
}

void FeedIconOverlay::ShowIcon(RE::Actor* target, const std::string& iconPath, float durationSeconds) {
    if (!target) {
        SKSE::log::warn("ShowIcon called with null target");
        return;
    }

    // Check if icon file exists first
    if (!std::filesystem::exists(iconPath)) {
        SKSE::log::warn("Icon file does not exist: {} - skipping icon display",
            iconPath);
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    // Just set the path and target - texture loading is handled in RenderOverlay
    _state.currentIconPath = iconPath;
    _state.target = target->GetHandle(); // Use handle
    _state.startTime = std::chrono::steady_clock::now();
    _state.duration = durationSeconds;
    _state.active.store(true);
    _state.feeding.store(false); // Reset feeding state
    _state.hasLastPos = false;  // Reset smoothing for new target

    SKSE::log::debug("Started icon overlay for target: {} (duration: {}s)",
        target->GetName(), durationSeconds);
}

void FeedIconOverlay::TriggerFeedAnimation() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state.active.load()) {
        _state.feeding.store(true);
        _state.feedStartTime = std::chrono::steady_clock::now();
        SKSE::log::debug("Triggered feed animation (bite)");
    }
}

void FeedIconOverlay::StopIcon() {
    std::lock_guard<std::mutex> lock(_mutex);
    _state.active.store(false);
    _state.feeding.store(false);
    _state.target.reset(); // Clear handle
    _state.hasLastPos = false;  // Reset smoothing state

    SKSE::log::debug("Stopped icon overlay");
}

void FeedIconOverlay::RenderOverlay() {
    // Check if overlay is active
    if (!_state.active.load()) {
        return;
    }

    // Handle expiry (unless feeding animation is active)
    if (!_state.feeding.load() && _state.IsExpired()) {
        SKSE::log::debug("Icon overlay expired, stopping");
        StopIcon();
        return;
    }

    // SKSE::log::trace("RenderOverlay called - overlay is active");

    std::lock_guard<std::mutex> lock(_mutex);

    // Validate target via handle
    auto target = _state.target.get();
    if (!target) {
        SKSE::log::debug("RenderOverlay: target is invalid, stopping");
        _state.active.store(false);
        _state.feeding.store(false);
        return;
    }

    auto settings = Settings::GetSingleton();
    float baseIconSize = settings->IconOverlay.IconSize;

    // --- Animation Logic ---
    auto now = std::chrono::steady_clock::now();
    AnimationResult animResult;

    if (_state.feeding.load()) {
        // Feeding Animation (Bite)
        auto feedElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - _state.feedStartTime).count() / 1000.0f;

        animResult = ImageAnimation::CalculateBiteAnimation(feedElapsed);

        if (animResult.isComplete) {
            // Animation done, hide icon
            _state.active.store(false);
            _state.feeding.store(false);
            return;
        }
    } else {
        // Standard Overlay Animation (Pulse/Float)
        auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - _state.startTime).count() / 1000.0f;

        animResult = ImageAnimation::CalculateOverlayImgAnimation(totalElapsed, _state.duration);
    }

    // Get icon position using new two-tier positioning strategy
    auto screenPos = GetAttachedObjectPos(target.get());

    // Check if positioning succeeded
    if (screenPos.x == 0.0f && screenPos.y == 0.0f) {
        return;
    }

    // Apply smoothing to reduce jitter from frame-to-frame position changes
    if (_state.hasLastPos) {
        screenPos.x = _state.lastScreenPos.x + (screenPos.x - _state.lastScreenPos.x) * SMOOTH_FACTOR;
        screenPos.y = _state.lastScreenPos.y + (screenPos.y - _state.lastScreenPos.y) * SMOOTH_FACTOR;
    }
    _state.lastScreenPos = ImGuiMCP::ImVec2(screenPos.x, screenPos.y);
    _state.hasLastPos = true;

    // Apply animation results
    float currentIconSize = baseIconSize * animResult.sizeScale;
    float finalW = currentIconSize * animResult.widthScale;
    float finalH = currentIconSize * animResult.heightScale;

    // Center the icon on the position
    ImGuiMCP::ImVec2 iconPos = ImGuiMCP::ImVec2(
        _state.lastScreenPos.x - finalW / 2.0f,
        _state.lastScreenPos.y - finalH / 2.0f
    );

    // Get foreground draw list to draw directly without creating a window
    auto* drawList = ImGuiMCP::GetForegroundDrawList();
    if (drawList) {
        // Load texture using framework (handles caching internally)
        auto textureID = SKSEMenuFramework::LoadTexture(_state.currentIconPath);

        if (textureID) {
             // Draw the texture directly on the screen (no window needed = no input blocking)
            ImGuiMCP::ImDrawListManager::AddImage(
                drawList,
                textureID,
                iconPos,
                ImGuiMCP::ImVec2(iconPos.x + finalW, iconPos.y + finalH),  // Bottom-right corner
                ImGuiMCP::ImVec2(0, 0),  // UV min
                ImGuiMCP::ImVec2(1, 1),  // UV max
                animResult.tintColor
            );
        }
    }
}
