#include "feed/FeedHealthBarOverlay.h"
#include "Settings.h"
#include <algorithm>

namespace {
    constexpr float SMOOTH_FACTOR = 0.3f;

    // Lerp green -> yellow -> red by remaining health fraction.
    ImGuiMCP::ImU32 HealthColor(float pct) {
        pct = std::clamp(pct, 0.0f, 1.0f);
        int r, g;
        if (pct > 0.5f) {
            // green (0,255) -> yellow (255,255)
            const float t = (1.0f - pct) * 2.0f;  // 0 at full, 1 at half
            r = static_cast<int>(255 * t);
            g = 255;
        } else {
            // yellow (255,255) -> red (255,0)
            const float t = pct * 2.0f;            // 1 at half, 0 at empty
            r = 255;
            g = static_cast<int>(255 * t);
        }
        return IM_COL32(r, g, 0, 230);
    }

    // Trailing "damage chip" color — a desaturated pale tone behind the front bar.
    constexpr ImGuiMCP::ImU32 TRAILING_COLOR = IM_COL32(235, 230, 225, 215);
}

FeedHealthBarOverlay* FeedHealthBarOverlay::GetSingleton() {
    static FeedHealthBarOverlay singleton;
    return &singleton;
}

void FeedHealthBarOverlay::Show(RE::Actor* target) {
    if (!target) return;
    auto* settings = Settings::GetSingleton();
    if (!settings->HealthBarOverlay.Enable) return;

    std::lock_guard<std::mutex> lock(_mutex);
    _target = target->GetHandle();
    _hasLastPos = false;
    _hasTrail = false;  // re-seed trailing layer for the new target
    _active.store(true);
    SKSE::log::debug("[HealthBar] Showing for {}", target->GetName());
}

void FeedHealthBarOverlay::Hide() {
    std::lock_guard<std::mutex> lock(_mutex);
    _active.store(false);
    _target.reset();
    _hasLastPos = false;
    SKSE::log::debug("[HealthBar] Hidden");
}

bool FeedHealthBarOverlay::WorldToScreen(const RE::NiPoint3& world, ImGuiMCP::ImVec2& outScreen) {
    static uintptr_t g_worldToCamMatrix = RELOCATION_ID(519579, 406126).address();
    static auto g_viewPort = (RE::NiRect<float>*)RELOCATION_ID(519618, 406160).address();

    float x = 0.0f, y = 0.0f, z = 0.0f;
    RE::NiCamera::WorldPtToScreenPt3((float(*)[4])g_worldToCamMatrix, *g_viewPort, world, x, y, z, 1e-5f);
    if (z <= 0.0f) return false;  // behind the camera

    const ImGuiMCP::ImVec2 rect = ImGuiMCP::GetIO()->DisplaySize;
    outScreen.x = rect.x * x;
    outScreen.y = rect.y * (1.0f - y);  // Y-axis flip for ImGui
    return true;
}

void FeedHealthBarOverlay::RenderOverlay() {
    if (!_active.load()) return;

    std::lock_guard<std::mutex> lock(_mutex);

    auto ref = _target.get();
    if (!ref) { _active.store(false); return; }
    auto* actor = ref->As<RE::Actor>();
    if (!actor) { _active.store(false); return; }

    auto* settings = Settings::GetSingleton();

    // Health fraction.
    auto* av = actor->AsActorValueOwner();
    if (!av) return;
    const float cur = av->GetActorValue(RE::ActorValue::kHealth);
    const float max = av->GetPermanentActorValue(RE::ActorValue::kHealth);
    if (max <= 0.0f) return;
    const float pct = std::clamp(cur / max, 0.0f, 1.0f);

    // Update the trailing "damage chip" layer: it holds for TrailingDelay after a
    // drain, then slides down toward the front bar at TrailingSpeed.
    const auto now = std::chrono::steady_clock::now();
    if (!_hasTrail) {
        _trailingPct = pct;
        _lastPct = pct;
        _lastDrainTime = now;
        _lastFrameTime = now;
        _hasTrail = true;
    } else {
        float dt = std::chrono::duration<float>(now - _lastFrameTime).count();
        dt = std::clamp(dt, 0.0f, 0.1f);
        _lastFrameTime = now;

        if (pct < _lastPct - 0.0005f) {
            _lastDrainTime = now;          // a drain just landed — restart the hold
        }
        if (pct > _trailingPct) {
            _trailingPct = pct;            // healed/rose: snap the trailing layer up
        } else if (_trailingPct > pct) {
            const float held = std::chrono::duration<float>(now - _lastDrainTime).count();
            if (held >= settings->HealthBarOverlay.TrailingDelay) {
                _trailingPct = std::max(pct, _trailingPct - settings->HealthBarOverlay.TrailingSpeed * dt);
            }
        }
        _lastPct = pct;
    }

    // World position above the head.
    RE::NiPoint3 worldPos;
    bool havePos = false;
    if (auto* middle = actor->GetMiddleHighProcess(); middle && middle->headNode) {
        worldPos = middle->headNode->world.translate;
        havePos = true;
    } else {
        worldPos = actor->GetPosition();
        worldPos.z += 120.0f;  // rough head height fallback
        havePos = true;
    }
    if (!havePos) return;
    worldPos.z += settings->HealthBarOverlay.HeightOffset * actor->GetScale();

    ImGuiMCP::ImVec2 screen;
    if (!WorldToScreen(worldPos, screen)) return;

    // Apply screen-space offsets (relative positioning).
    screen.x += settings->HealthBarOverlay.OffsetX;
    screen.y += settings->HealthBarOverlay.OffsetY;

    // Smooth to reduce jitter.
    if (_hasLastPos) {
        screen.x = _lastScreenPos.x + (screen.x - _lastScreenPos.x) * SMOOTH_FACTOR;
        screen.y = _lastScreenPos.y + (screen.y - _lastScreenPos.y) * SMOOTH_FACTOR;
    }
    _lastScreenPos = screen;
    _hasLastPos = true;

    const float scale = std::max(0.05f, settings->HealthBarOverlay.Scale);
    const float w = settings->HealthBarOverlay.Width * scale;
    const float h = settings->HealthBarOverlay.Height * scale;
    const ImGuiMCP::ImVec2 pMin{ screen.x - w * 0.5f, screen.y - h * 0.5f };
    const ImGuiMCP::ImVec2 pMax{ screen.x + w * 0.5f, screen.y + h * 0.5f };

    auto* dl = ImGuiMCP::GetForegroundDrawList();
    if (!dl) return;

    // Background.
    ImGuiMCP::ImDrawListManager::AddRectFilled(dl, pMin, pMax, IM_COL32(20, 20, 20, 180), 2.0f, 0);
    // Trailing "damage chip" layer behind the front bar — only the gap between the
    // (lower) front bar and the (higher) trailing value is visible.
    if (settings->HealthBarOverlay.EnableTrailing && _trailingPct > pct) {
        const ImGuiMCP::ImVec2 trailMax{ pMin.x + w * _trailingPct, pMax.y };
        ImGuiMCP::ImDrawListManager::AddRectFilled(dl, pMin, trailMax, TRAILING_COLOR, 2.0f, 0);
    }
    // Front bar — actual current health, snaps instantly.
    const ImGuiMCP::ImVec2 fillMax{ pMin.x + w * pct, pMax.y };
    ImGuiMCP::ImDrawListManager::AddRectFilled(dl, pMin, fillMax, HealthColor(pct), 2.0f, 0);
    // Border.
    ImGuiMCP::ImDrawListManager::AddRect(dl, pMin, pMax, IM_COL32(0, 0, 0, 220), 2.0f, 0, 1.5f);
}
