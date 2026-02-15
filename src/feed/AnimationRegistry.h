#pragma once
#include <string>
#include <vector>
#include <optional>
#include <RE/Skyrim.h>

namespace Feed {

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

}
