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

    // Approach direction. Front/Back are matched against the player's position for
    // upright feeds; Left/Right encode which side of the furniture the player feeds
    // from for bed/bedroll packs (matched against playerOnLeft). Any always matches.
    enum class Direction { Front, Back, Any, Left, Right };
    enum class Sex { Unisex, Female, Male };
    enum class Type { Normal, Combat };

    // Furniture context a composite pack is authored for. None = a free-standing
    // (upright) feed; Bed/Bedroll = the victim is lying in that furniture and the
    // player plays a side-of-bed clip. Drives composite pack selection so a
    // furniture pack is never rolled for a standing feed (and vice versa).
    enum class Furniture { None, Bed, Bedroll };

    struct AnimationDefinition {
        std::string eventName; // Unique identifier from JSON key
        Direction direction = Direction::Front;
        Sex sex = Sex::Unisex;
        Type type = Type::Normal;
        bool isHungry = false;
        bool isLethal = false;
        int feedTypeID = 0; // Value for GraphVariable SkyPromptFeedType
    };

    // Raw NotifyAnimationGraph event names for one stage of a composite feed.
    // Either side may be empty (skips that actor's clip for the stage).
    struct StageClips {
        std::string player;
        std::string target;
    };

    // A staged composite animation set. Two outcomes branch out of the Loop:
    //   - victim drained dry IN the Loop  -> killed inline, no further clip
    //   - player stops before dry         -> Exit (GoBack) -> Drained idle aftermath
    // So `drained` is the survivor's woozy idle that plays after Exit; the kill is
    // a Loop-stage action (drain only happens in the Loop), not a separate clip.
    // Direction/sex/isHungry drive pack selection (Phase 2).
    struct CompositePack {
        std::string name;
        Direction direction = Direction::Front;
        Sex sex = Sex::Unisex;
        bool isHungry = false;
        // Furniture this pack is authored for (default: free-standing feed). The
        // left/right side is carried by `direction` (Left/Right) for furniture packs.
        Furniture furniture = Furniture::None;
        // True when NO stage carries a target clip: only the player is animated and
        // the victim is left in place (no embrace-lock / collision / AI changes).
        // Auto-detected at load time from the stage clips.
        bool playerOnly = false;
        StageClips intro, loop, exit, drained;
    };

    struct FeedContext {
        bool isCombat;
        bool isSneaking;
        bool isHungry;      // Based on hunger stage
        bool isBehind;
        bool targetIsStanding; // true=Standing, false=Sleeping/Sitting
        bool isLethal = false;  // User selected lethal feed option
        // Furniture the victim is in (None for a standing/upright feed) and which
        // side the player is on. Used by GetBestCompositeMatch to pick a furniture
        // pack and its left/right variant.
        Furniture furniture = Furniture::None;
        bool playerOnLeft = false;
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

        // Find the best matching staged composite pack for the current context
        // (filters by direction, sex, hunger). Returns nullptr if none loaded/match.
        const CompositePack* GetBestCompositeMatch(const FeedContext& context) const;

        // True if any loaded composite pack is authored for a Back (behind)
        // approach. Lets the feed path force a front feed when no back animation
        // exists, instead of rotating the victim to face away with no clip.
        bool HasCompositeBackPack() const;

        // Get the next animation in sequence (debug mode)
        // Filters to only cycle through contextually appropriate animations
        const AnimationDefinition* GetNextDebugAnimation(const FeedContext& context);

        // Clear all loaded animations (for reload)
        void Clear();

        size_t GetLoadedCount() const { return animations_.size(); }
        size_t GetLoadedCompositeCount() const { return compositePacks_.size(); }

    private:
        std::vector<AnimationDefinition> animations_;
        std::vector<CompositePack> compositePacks_;
    };

    // Fallback animation selection (legacy logic for when no OAR animations are loaded)
    const char* SelectIdleAnimation(int targetState, RE::Actor* target,
                                    const RE::NiPointer<RE::TESObjectREFR>& furnitureRef, bool isBehind,
                                    bool& outIsPairedAnim, bool lethal = false);

}
