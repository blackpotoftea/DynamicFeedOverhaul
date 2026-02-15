#pragma once

// Custom paired animation feed - replaces InitiateVampireFeedPackage
namespace CustomFeed {
    // Idle EditorIDs - TODO: replace with actual Skyrim.esm names
    namespace Idles {
        // Standing
        inline constexpr const char* VAMPIRE_STANDING_FRONT = "IdleVampireStandingFront";
        inline constexpr const char* VAMPIRE_STANDING_BACK = "IdleVampireStandingBack";
        // Bed
        inline constexpr const char* VAMPIRE_BED_LEFT = "VampireFeedingBedLeft_Loose";
        inline constexpr const char* VAMPIRE_BED_RIGHT = "VampireFeedingBedRight_Loose";
        // Bedroll
        inline constexpr const char* VAMPIRE_BEDROLL_LEFT = "VampireFeedingBedRollLeft_Loose";
        inline constexpr const char* VAMPIRE_BEDROLL_RIGHT = "VampireFeedingBedRollRight_Loose";
        // Sitting
        inline constexpr const char* VAMPIRE_SITTING_FRONT = "VampireFeedSittingFront";
        inline constexpr const char* VAMPIRE_SITTING_BACK = "VampireFeedSittingBack";


        // Standing
        inline constexpr const char* VAMPIRELORD_STANDING_FRONT = "VampireLordLeftPowerAttackFeedFront";
        inline constexpr const char* VAMPIRELORD_STANDING_BACK = "VampireLordLeftPowerAttackFeedBack";


        inline constexpr const char* CANIBAL_STANDING_FRONT = "IdleCannibalFeedStanding";
        inline constexpr const char* CANIBAL_STANDING_CROUCH = "IdleCannibalFeedCrouching";


        inline constexpr const char* WEREWOLF_STANDING_FRONT = "WerewolfPairedFeedingWithHuman";
        //WerewolfPairedMaulingWithHuman SpecialFeeding
    }

    // Feed target management - uses ObjectRefHandle for safe persistence
    void SetFeedTarget(RE::Actor* target);
    void ClearFeedTarget();
    RE::NiPointer<RE::Actor> GetFeedTarget();

    // Utility functions
    bool IsPlayerOnLeftSide(RE::Actor* target);
    bool IsBedroll(RE::TESObjectREFR* furniture);

    // Animation playback
    bool PlayPairedFeed(const char* idleEditorID, RE::Actor* target, bool isPaired = true);
    void ForceStop();
    void OnComplete();
}
