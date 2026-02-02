#pragma once

// Custom paired animation feed - replaces InitiateVampireFeedPackage
namespace CustomFeed {
    // Idle EditorIDs - TODO: replace with actual Skyrim.esm names
    namespace Idles {
        // Standing
        inline constexpr const char* STANDING_FRONT = "IdleVampireStandingFront";  // placeholder
        inline constexpr const char* STANDING_BACK = "IdleVampireStandingBack";    // placeholder
        // Bed
        inline constexpr const char* BED_LEFT = "VampireFeedingBedLeft_Loose";              // placeholder
        inline constexpr const char* BED_RIGHT = "VampireFeedingBedRight_Loose";            // placeholder
        // Bedroll
        inline constexpr const char* BEDROLL_LEFT = "VampireFeedingBedRollLeft_Loose";      // placeholder
        inline constexpr const char* BEDROLL_RIGHT = "VampireFeedingBedRollRight_Loose";    // placeholder
        // Sitting
        inline constexpr const char* SITTING_FRONT = "j";    // placeholder
        inline constexpr const char* SITTING_BACK = "VampireFeedSittingBack";      // placeholder
        // Combat
        inline constexpr const char* COMBAT_FRONT = "VampireFeedCombatFront";      // placeholder
        inline constexpr const char* COMBAT_BACK = "VampireFeedCombatBack";        // placeholder
    }

    // State tracking for recovery
    inline RE::Actor* feedTarget_ = nullptr;

    // Check if player is on left or right side of target (for bed animations)
    inline bool IsPlayerOnLeftSide(RE::Actor* target) {
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
    inline bool IsBedroll(RE::TESObjectREFR* furniture) {
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

    // Flag to control AI-driven state (only for package-based animations)
    inline bool useAIDriven_ = false;

    // Play feed animation by EditorID
    // For standing/combat: paired animation with target
    // For bed/bedroll: solo idle on player only (no target involvement)
    // Returns true if animation started successfully
    inline bool PlayPairedFeed(const char* idleEditorID, RE::Actor* target, bool isPaired = true) {
        SKSE::log::info("[CustomFeed] PlayPairedFeed called (paired={})", isPaired);

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

        SKSE::log::info("[CustomFeed] Looking up idle: '{}'", idleEditorID);
        if (target) {
            SKSE::log::info("[CustomFeed] Target: {} (FormID: {:X})", target->GetName(), target->GetFormID());
        }

        feedTarget_ = target;

        // Lookup idle form
        auto* feedIdle = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(idleEditorID);
        if (!feedIdle) {
            SKSE::log::error("[CustomFeed] FAILED: Idle '{}' not found in game data", idleEditorID);
            feedTarget_ = nullptr;
            return false;
        }
        SKSE::log::info("[CustomFeed] Found idle form: {:X}", feedIdle->GetFormID());

        // Get process
        auto* process = player->GetActorRuntimeData().currentProcess;
        if (!process) {
            SKSE::log::error("[CustomFeed] FAILED: player->currentProcess is null");
            feedTarget_ = nullptr;
            return false;
        }
        SKSE::log::info("[CustomFeed] Got player process");

        // AI-driven is only for package-based animations, not for PlayIdle
        // Disabled by default
        if (useAIDriven_) {
            SKSE::log::info("[CustomFeed] Setting player AI-driven...");
            player->SetAIDriven(true);
        }

        // Play idle
        SKSE::log::info("[CustomFeed] Calling PlayIdle (paired={})...", isPaired);
        bool success;
        if (isPaired) {
            // Paired animation - target participates
            success = process->PlayIdle(player, feedIdle, target);
        } else {
            // Solo idle - just player, no target at all
            success = process->PlayIdle(player, feedIdle, nullptr);
        }
        SKSE::log::info("[CustomFeed] PlayIdle returned: {}", success);

        if (!success) {
            SKSE::log::error("[CustomFeed] FAILED: PlayIdle returned false");
            if (useAIDriven_) {
                player->SetAIDriven(false);
            }
            feedTarget_ = nullptr;
            return false;
        }

        SKSE::log::info("[CustomFeed] SUCCESS: Animation started");
        return true;
    }

    // Force stop - recovery function for stuck animations
    inline void ForceStop() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->SetAIDriven(false);
            if (auto* process = player->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(player, true);
            }
        }
        if (feedTarget_) {
            if (auto* process = feedTarget_->GetActorRuntimeData().currentProcess) {
                process->StopCurrentIdle(feedTarget_, true);
            }
            feedTarget_ = nullptr;
        }
        SKSE::log::info("[CustomFeed] ForceStop called");
    }

    // Called when feed ends normally - restores player control
    inline void OnComplete() {
        SKSE::log::info("[CustomFeed] OnComplete - restoring player control");
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            player->SetAIDriven(false);
            SKSE::log::info("[CustomFeed] SetAIDriven(false) called");
        }
        feedTarget_ = nullptr;
    }

    // Get current feed target (for external checks)
    inline RE::Actor* GetFeedTarget() {
        return feedTarget_;
    }
}
