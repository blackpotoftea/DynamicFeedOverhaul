#include "CustomFeed.h"
#include "utils/AnimUtil.h"

namespace CustomFeed {
    // Private state - hidden from header, stored as ObjectRefHandle for memory safety
    // THREADING: Must only be accessed from main game thread (ensured via SKSE::GetTaskInterface()->AddTask)
    static RE::ObjectRefHandle feedTargetHandle_{};

    // Set feed target - stores ObjectRefHandle for safe persistence
    void SetFeedTarget(RE::Actor* target) {
        if (target) {
            feedTargetHandle_ = target->GetHandle();
        } else {
            feedTargetHandle_ = {};
        }
    }

    // Clear feed target
    void ClearFeedTarget() {
        feedTargetHandle_ = {};
    }

    // Get current feed target - safely validates handle before returning
    // Returns NiPointer to keep reference alive in caller's scope
    RE::NiPointer<RE::Actor> GetFeedTarget() {
        // Resolve the handle - .get() checks if object is loaded and valid
        auto ref = feedTargetHandle_.get();

        if (!ref) {
            // Actor no longer exists, is deleted, or unloaded
            feedTargetHandle_ = {};
            return nullptr;
        }

        // Cast to Actor (ObjectRefHandle stores TESObjectREFR, we need Actor)
        auto* actor = ref->As<RE::Actor>();

        // Perform gameplay logic checks
        if (!actor || actor->IsDead() || actor->IsDisabled()) {
            feedTargetHandle_ = {}; // Clear handle if target is invalid for gameplay
            return nullptr;
        }

        // Return smart pointer - keeps reference alive in caller's scope
        return RE::NiPointer<RE::Actor>(actor);
    }

    // Check if player is on left or right side of target (for bed animations)
    bool IsPlayerOnLeftSide(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return true;

        auto playerPos = player->GetPosition();
        auto targetPos = target->GetPosition();
        float targetHeading = target->GetAngleZ();

        float dx = playerPos.x - targetPos.x;
        float dy = playerPos.y - targetPos.y;

        // Cross product to determine side relative to target's facing
        float cross = std::cos(targetHeading) * dx - std::sin(targetHeading) * dy;
        return cross > 0;
    }

    // Check if furniture is a bedroll (vs regular bed) - case insensitive
    bool IsBedroll(RE::TESObjectREFR* furniture) {
        if (!furniture) return false;

        auto baseObj = furniture->GetBaseObject();
        if (!baseObj) return false;

        auto editorID = baseObj->GetFormEditorID();
        if (editorID) {
            std::string lower(editorID);
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return lower.find("bedroll") != std::string::npos;
        }
        return false;
    }

    // Play feed animation by EditorID
    // For standing/combat: paired animation with target
    // For bed/bedroll: solo idle on player only (no target involvement)
    // Returns true if animation started successfully
    bool PlayPairedFeed(const char* idleEditorID, RE::Actor* target, bool isPaired) {
        SKSE::log::debug("[CustomFeed] PlayPairedFeed called (paired={})", isPaired);

        // Validate inputs
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("[CustomFeed] FAILED: player is null");
            return false;
        }
        if (!idleEditorID) {
            SKSE::log::error("[CustomFeed] FAILED: idleEditorID is null");
            return false;
        }
        // Target only required for paired animations
        if (isPaired && !target) {
            SKSE::log::error("[CustomFeed] FAILED: target is null for paired animation");
            return false;
        }

        SKSE::log::debug("[CustomFeed] Looking up idle: '{}'", idleEditorID);
        if (target) {
            SKSE::log::debug("[CustomFeed] Target: {} (FormID: {:X})", target->GetName(), target->GetFormID());
        }

        SetFeedTarget(target);

        // Lookup idle form
        auto* feedIdle = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(idleEditorID);
        if (!feedIdle) {
            SKSE::log::error("[CustomFeed] FAILED: Idle '{}' not found in game data", idleEditorID);
            ClearFeedTarget();
            return false;
        }
        SKSE::log::debug("[CustomFeed] Found idle form: {:X}", feedIdle->GetFormID());

        // Use AnimUtil::playIdle for thread-safe, handle-based animation playback
        SKSE::log::debug("[CustomFeed] Calling AnimUtil::playIdle (paired={})...", isPaired);

        // AnimUtil::playIdle handles thread-safety and ObjectRefHandle automatically
        AnimUtil::playIdle(player, feedIdle, isPaired ? target : nullptr);

        SKSE::log::info("[CustomFeed] SUCCESS: Animation playback initiated");
        return true;
    }

    // Force stop - recovery function for stuck animations
    void ForceStop() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            if (auto* process = player->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(player, true);
            }
        }
        // Safely retrieve target actor via handle (returns smart pointer)
        if (auto targetPtr = GetFeedTarget()) {
            if (auto* process = targetPtr->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(targetPtr.get(), true);
            }
        }
        ClearFeedTarget();
        SKSE::log::info("[CustomFeed] ForceStop called");
    }

    // Called when feed ends normally - restores player control
    void OnComplete() {
        SKSE::log::debug("[CustomFeed] OnComplete");
        auto* player = RE::PlayerCharacter::GetSingleton();
        // if (player) {
        //     player->SetAIDriven(false);
        //     SKSE::log::debug("[CustomFeed] SetAIDriven(false) called");
        // }
        ClearFeedTarget();
    }
}
