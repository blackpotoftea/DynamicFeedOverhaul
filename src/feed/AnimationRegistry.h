#pragma once
#include <string>
#include <vector>
#include <optional>
#include <RE/Skyrim.h>

namespace Feed {

    // Target state constants for feed type calculation
    // These represent base values multiplied by 10 in OAR graph variable conditions
    // Format: (TargetState * 10) + VampireHungerStage
    constexpr int kStanding = 10;
    constexpr int kSleeping = 20;
    constexpr int kSitting = 30;
    constexpr int kCombat = 40;
    constexpr int kDead = 50;

    // Idle EditorIDs for fallback animation selection
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

        // Vampire Lord Standing
        inline constexpr const char* VAMPIRELORD_STANDING_FRONT = "VampireLordLeftPowerAttackFeedFront";
        inline constexpr const char* VAMPIRELORD_STANDING_BACK = "VampireLordLeftPowerAttackFeedBack";

        // Cannibal
        inline constexpr const char* CANIBAL_STANDING_FRONT = "IdleCannibalFeedStanding";
        inline constexpr const char* CANIBAL_STANDING_CROUCH = "IdleCannibalFeedCrouching";

        // Werewolf
        inline constexpr const char* WEREWOLF_STANDING_FRONT = "WerewolfPairedFeedingWithHuman";
        inline constexpr const char* WEREWOLF_CORPSE_FEED = "SpecialFeeding";

        // Combat idle Standing
        inline constexpr const char* FRONT_KM_A = "IdleVampireStandingFront"; // "1HMKillMoveRepeatStabDowns"; //pa_1HMKillMoveShortA

        // Combat idle back
        inline constexpr const char* BACK_SNEAK_KM_A ="IdleVampireStandingBack"; // "KillMoveBackStab"; //pa_1HMSneakKillBackA
    }

    enum class Direction { Front, Back, Any };
    enum class Sex { Unisex, Female, Male };
    enum class Type { Normal, Combat };

    struct AnimationDefinition {
        std::string eventName; // Unique identifier from JSON key
        Direction direction = Direction::Front;
        Sex sex = Sex::Unisex;
        Type type = Type::Normal;
        bool isHungry = false;
        bool isLethal = false;
        int feedTypeID = 0; // Value for GraphVariable SkyPromptFeedType
    };

    struct FeedContext {
        bool isCombat;
        bool isSneaking;
        bool isHungry;      // Based on hunger stage
        bool isBehind;
        bool targetIsStanding; // true=Standing, false=Sleeping/Sitting
        bool isLethal = false;  // User selected lethal feed option
        RE::Actor* player;
        RE::Actor* target;
    };

    class AnimationRegistry {
    public:
        static AnimationRegistry* GetSingleton();

        // Load all *_DPA.json files from the specified directory
        void LoadAnimations(const std::string& directoryPath);

        // Find the best matching animation for the current context
        // Returns nullptr if no match found
        const AnimationDefinition* GetBestMatch(const FeedContext& context) const;

        // Get the next animation in sequence (debug mode)
        // Filters to only cycle through contextually appropriate animations
        const AnimationDefinition* GetNextDebugAnimation(const FeedContext& context);

        // Clear all loaded animations (for reload)
        void Clear();

        size_t GetLoadedCount() const { return animations_.size(); }

    private:
        std::vector<AnimationDefinition> animations_;
    };

    // Fallback animation selection (legacy logic for when no OAR animations are loaded)
    const char* SelectIdleAnimation(int targetState, RE::Actor* target,
                                    const RE::NiPointer<RE::TESObjectREFR>& furnitureRef, bool isBehind,
                                    bool& outIsPairedAnim, bool lethal = false);

}
