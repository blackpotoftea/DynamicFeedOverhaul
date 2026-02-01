#pragma once
#include "SkyPrompt/API.hpp"
#include "Settings.h"
#include "TargetState.h"
#include "PapyrusCall.h"
#include "util.h"
#include <random>
#include <mutex>

extern SkyPromptAPI::ClientID g_clientID;

// Check if an actor is currently in a paired animation (vampire feed, killmove, etc.)
// Uses TESConditionItem with GetPairedAnimation function
namespace PairedAnimation {
    inline bool IsInPairedAnimation(const RE::Actor* actor) {
        if (!actor) return false;

        static RE::TESConditionItem cond;
        static std::once_flag flag;
        std::call_once(flag, [&]() {
            cond.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetPairedAnimation;
            cond.data.comparisonValue.f = 0.0f;
            cond.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kNotEqualTo;
            cond.data.object = RE::CONDITIONITEMOBJECT::kSelf;
        });

        RE::ConditionCheckParams params(const_cast<RE::TESObjectREFR*>(actor->As<RE::TESObjectREFR>()), nullptr);
        return cond(params);
    }
}

// Feed type calculation for OAR graph variable conditions
// Composite value: (TargetState * 10) + VampireHungerStage
// This allows OAR to match on both target state and hunger level
//
// Target State (tens digit):
//   1x = Standing
//   2x = Sleeping
//   3x = Sitting
//   4x = Combat
//
// Vampire Hunger Stage (ones digit):
//   x1 = Stage 1 (sated)
//   x2 = Stage 2
//   x3 = Stage 3
//   x4 = Stage 4 (blood starved)
//
// Examples:
//   11 = Standing, Stage 1 (sated)
//   14 = Standing, Stage 4 (blood starved)
//   21 = Sleeping, Stage 1
//   44 = Combat, Stage 4 (most aggressive)
//
// OAR conditions can match:
//   == 14 : exactly standing + stage 4
//   >= 40 : any combat feed
//   >= 13 AND < 20 : standing + stage 3 or 4

namespace FeedState {
    // Target state base values (multiply by 10)
    constexpr int kStanding = 10;
    constexpr int kSleeping = 20;
    constexpr int kSitting = 30;
    constexpr int kCombat = 40;

    // Calculate feed type from target state and vampire hunger stage
    inline int Calculate(int targetState, int vampireStage) {
        // Clamp vampire stage to 1-4
        int stage = std::clamp(vampireStage, 1, 4);
        return targetState + stage;
    }

    // Pick random element from vector
    inline int PickRandom(const std::vector<int>& list) {
        if (list.empty()) return 0;
        if (list.size() == 1) return list[0];

        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dist(0, list.size() - 1);
        return list[dist(gen)];
    }

    // Player gender for animation selection
    enum class Gender { kMale, kFemale };

    // Get player gender
    inline Gender GetPlayerGender() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            auto* base = player->GetActorBase();
            if (base && base->IsFemale()) {
                return Gender::kFemale;
            }
        }
        return Gender::kMale;
    }

    // Calculate angle from target to player (in radians)
    inline float GetAngleToPlayer(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return 0.0f;

        auto playerPos = player->GetPosition();
        auto targetPos = target->GetPosition();

        float dx = playerPos.x - targetPos.x;
        float dy = playerPos.y - targetPos.y;

        // atan2 gives angle from target to player
        return std::atan2(dx, dy);
    }

    // Rotate target to face toward or away from player (whichever is closer)
    // Returns true if rotated to face away (back animation), false if facing toward (front animation)
    inline bool RotateTargetToClosest(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return false;

        float angleToPlayer = GetAngleToPlayer(target);
        float currentHeading = target->GetAngleZ();

        // Normalize angle difference to -PI to PI
        float diffToFront = angleToPlayer - currentHeading;
        while (diffToFront > 3.14159f) diffToFront -= 6.28318f;
        while (diffToFront < -3.14159f) diffToFront += 6.28318f;

        // Back angle is opposite (180 degrees / PI radians)
        float backAngle = angleToPlayer + 3.14159f;
        float diffToBack = backAngle - currentHeading;
        while (diffToBack > 3.14159f) diffToBack -= 6.28318f;
        while (diffToBack < -3.14159f) diffToBack += 6.28318f;

        bool useBack = std::fabs(diffToBack) < std::fabs(diffToFront);
        float newAngle = useBack ? backAngle : angleToPlayer;

        // Normalize new angle to 0 to 2PI
        while (newAngle < 0) newAngle += 6.28318f;
        while (newAngle >= 6.28318f) newAngle -= 6.28318f;

        SKSE::log::info("Rotate target: current={:.2f}, toFront={:.2f} (diff={:.2f}), toBack={:.2f} (diff={:.2f}) -> {} (new={:.2f})",
            currentHeading, angleToPlayer, diffToFront, backAngle, diffToBack,
            useBack ? "BACK" : "FRONT", newAngle);

        // SetAngle takes NiPoint3 with x, y, z angles - we only change z (heading)
        RE::NiPoint3 angles = target->data.angle;
        angles.z = newAngle;
        target->SetAngle(angles);
        return useBack;
    }

    // Select FeedType based on combat state, hunger level, position, and player gender
    // Priority: position (front/back) > gender (female > unisex fallback)
    // Returns 0 if no animations configured (falls back to default calculation)
    inline int SelectAnimation(bool isInCombat, int vampireStage, RE::Actor* target) {
        auto* settings = Settings::GetSingleton();

        if (!settings->Animation.EnableRandomSelection) {
            return 0;  // Use default calculation
        }

        bool isHungry = vampireStage >= settings->Animation.HungryThreshold;
        bool isBehind;

        if (settings->General.SequentialPlay) {
            // Debug mode: sequential animation playthrough
            // 1. Determine front/back based on closest rotation
            // 2. Collect all unisex animations for that direction (ignore hunger/gender)
            // 3. Play sequentially, cycling through all before repeating

            static std::vector<int> sequentialList;
            static size_t sequentialIndex = 0;
            static bool lastWasBack = false;

            // Determine position and rotate target
            isBehind = RotateTargetToClosest(target);

            // If direction changed or list is empty, rebuild the list
            if (sequentialList.empty() || isBehind != lastWasBack) {
                sequentialList.clear();
                sequentialIndex = 0;
                lastWasBack = isBehind;

                auto& a = settings->Animation;
                if (isBehind) {
                    sequentialList.insert(sequentialList.end(), a.SatedBackUnisex.begin(), a.SatedBackUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.HungryBackUnisex.begin(), a.HungryBackUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.CombatSatedBackUnisex.begin(), a.CombatSatedBackUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.CombatHungryBackUnisex.begin(), a.CombatHungryBackUnisex.end());
                } else {
                    sequentialList.insert(sequentialList.end(), a.SatedFrontUnisex.begin(), a.SatedFrontUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.HungryFrontUnisex.begin(), a.HungryFrontUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.CombatSatedFrontUnisex.begin(), a.CombatSatedFrontUnisex.end());
                    sequentialList.insert(sequentialList.end(), a.CombatHungryFrontUnisex.begin(), a.CombatHungryFrontUnisex.end());
                }

                SKSE::log::info("SequentialPlay: rebuilt {} list with {} animations",
                    isBehind ? "back" : "front", sequentialList.size());
            }

            if (!sequentialList.empty()) {
                // Wrap around if we've gone through all
                if (sequentialIndex >= sequentialList.size()) {
                    sequentialIndex = 0;
                    SKSE::log::info("SequentialPlay: cycling back to start");
                }

                int selected = sequentialList[sequentialIndex];
                SKSE::log::info("SequentialPlay: {} [{}/{}] FeedType={}",
                    isBehind ? "back" : "front", sequentialIndex + 1, sequentialList.size(), selected);
                sequentialIndex++;
                return selected;
            }

            SKSE::log::warn("SequentialPlay: no {} animations configured", isBehind ? "back" : "front");
            return 0;
        }

        // Normal mode: rotate target to closest direction and select based on all criteria
        isBehind = RotateTargetToClosest(target);
        Gender gender = GetPlayerGender();

        const std::vector<int>* femaleList = nullptr;
        const std::vector<int>* unisexList = nullptr;

        if (isInCombat) {
            if (isHungry) {
                if (isBehind) {
                    femaleList = &settings->Animation.CombatHungryBackFemale;
                    unisexList = &settings->Animation.CombatHungryBackUnisex;
                } else {
                    femaleList = &settings->Animation.CombatHungryFrontFemale;
                    unisexList = &settings->Animation.CombatHungryFrontUnisex;
                }
            } else {
                if (isBehind) {
                    femaleList = &settings->Animation.CombatSatedBackFemale;
                    unisexList = &settings->Animation.CombatSatedBackUnisex;
                } else {
                    femaleList = &settings->Animation.CombatSatedFrontFemale;
                    unisexList = &settings->Animation.CombatSatedFrontUnisex;
                }
            }
        } else {
            if (isHungry) {
                if (isBehind) {
                    femaleList = &settings->Animation.HungryBackFemale;
                    unisexList = &settings->Animation.HungryBackUnisex;
                } else {
                    femaleList = &settings->Animation.HungryFrontFemale;
                    unisexList = &settings->Animation.HungryFrontUnisex;
                }
            } else {
                if (isBehind) {
                    femaleList = &settings->Animation.SatedBackFemale;
                    unisexList = &settings->Animation.SatedBackUnisex;
                } else {
                    femaleList = &settings->Animation.SatedFrontFemale;
                    unisexList = &settings->Animation.SatedFrontUnisex;
                }
            }
        }

        const char* posStr = isBehind ? "back" : "front";

        // For female players, try female-specific list first
        if (gender == Gender::kFemale && femaleList && !femaleList->empty()) {
            int selected = PickRandom(*femaleList);
            SKSE::log::info("Animation selection: combat={}, hungry={}, pos={}, gender=female, FeedType={} (female-specific)",
                isInCombat, isHungry, posStr, selected);
            return selected;
        }

        // Use unisex list (for male players, or female fallback)
        if (unisexList && !unisexList->empty()) {
            int selected = PickRandom(*unisexList);
            SKSE::log::info("Animation selection: combat={}, hungry={}, pos={}, gender={}, FeedType={} (unisex)",
                isInCombat, isHungry, posStr, (gender == Gender::kFemale ? "female" : "male"), selected);
            return selected;
        }

        return 0;  // No animations configured, use default
    }
}

// Graph variable names for OAR conditions (injected by Behavior Data Injector)
namespace GraphVars {
    // Bool: true when vampire feed is active
    inline constexpr auto IsSkyPromptFeeding = "IsSkyPromptFeeding";
    // Int: composite feed type (TargetState * 10 + HungerStage)
    inline constexpr auto SkyPromptFeedType = "SkyPromptFeedType";
}

class VampireFeedSink : public SkyPromptAPI::PromptSink {
public:
    static VampireFeedSink* GetSingleton() {
        static VampireFeedSink singleton;
        return &singleton;
    }

    std::span<const SkyPromptAPI::Prompt> GetPrompts() const override {
        return prompts_;
    }

    void ProcessEvent(SkyPromptAPI::PromptEvent event) const override {
        SKSE::log::debug("ProcessEvent called - type: {}", static_cast<int>(event.type));

        switch (event.type) {
        case SkyPromptAPI::PromptEventType::kAccepted:
            if (currentTarget_) {
                SKSE::log::info("Feed ACCEPTED on target: {} (FormID: {:X})",
                    currentTarget_->GetName(), currentTarget_->GetFormID());

                auto player = RE::PlayerCharacter::GetSingleton();
                if (!player) break;

                // Check target state
                auto sitSleepState = TargetState::GetSitSleepState(currentTarget_);
                bool isInFurniture = TargetState::IsInFurnitureState(currentTarget_);
                auto furnitureType = TargetState::GetActorFurnitureType(currentTarget_);
                auto furnitureRef = TargetState::GetFurnitureReference(currentTarget_);

                SKSE::log::info("Target state: {} | InFurniture: {} | FurnitureType: {}",
                    TargetState::SitSleepStateToString(sitSleepState),
                    isInFurniture,
                    TargetState::FurnitureTypeToString(furnitureType));

                // Determine target state for feed type calculation
                int targetState = FeedState::kStanding;
                bool isInCombat = currentTarget_->IsInCombat();

                if (isInCombat) {
                    SKSE::log::info("Target is in COMBAT - using combat feed");
                    targetState = FeedState::kCombat;
                } else if (TargetState::IsSleeping(currentTarget_)) {
                    SKSE::log::info("Target is SLEEPING - using bed feed");
                    targetState = FeedState::kSleeping;
                } else if (TargetState::IsSitting(currentTarget_)) {
                    SKSE::log::info("Target is SITTING - using seated feed");
                    targetState = FeedState::kSitting;
                } else if (TargetState::IsStanding(currentTarget_)) {
                    SKSE::log::info("Target is STANDING - using standing feed");
                    targetState = FeedState::kStanding;

                    // Pre-position to fix stairs animation issue
                    // Move the lower actor up to the higher one's Z position
                    auto* settings = Settings::GetSingleton();
                    if (settings->NonCombat.EnableHeightAdjust) {
                        auto playerPos = player->GetPosition();
                        auto targetPos = currentTarget_->GetPosition();
                        float heightDiff = std::fabs(targetPos.z - playerPos.z);

                        if (heightDiff > settings->NonCombat.MinHeightDiff &&
                            heightDiff <= settings->NonCombat.MaxHeightDiff) {
                            float higherZ = std::max(playerPos.z, targetPos.z);

                            if (playerPos.z < targetPos.z) {
                                // Player is lower - move player up to target
                                SKSE::log::info("Height diff: {:.1f} - moving player up from {:.1f} to {:.1f}",
                                    heightDiff, playerPos.z, higherZ);
                                player->SetPosition(RE::NiPoint3(playerPos.x, playerPos.y, higherZ), true);
                            } else {
                                // Target is lower - move target up to player
                                SKSE::log::info("Height diff: {:.1f} - moving target up from {:.1f} to {:.1f}",
                                    heightDiff, targetPos.z, higherZ);
                                currentTarget_->SetPosition(RE::NiPoint3(targetPos.x, targetPos.y, higherZ), true);
                            }
                        } else if (heightDiff > settings->NonCombat.MaxHeightDiff) {
                            SKSE::log::warn("Height diff {:.1f} exceeds max {:.1f} - skipping repositioning",
                                heightDiff, settings->NonCombat.MaxHeightDiff);
                        }
                    }
                }

                // Get vampire hunger stage and calculate/select feed type
                auto* settings = Settings::GetSingleton();
                int vampireStage = PapyrusCall::GetVampireStage();
                int feedType;

                if (settings->General.ForceFeedType > 0) {
                    // Debug override
                    feedType = settings->General.ForceFeedType;
                    SKSE::log::info("Using FORCED FeedType: {}", feedType);
                } else {
                    // Try random selection from configured animations
                    feedType = FeedState::SelectAnimation(isInCombat, vampireStage, currentTarget_);

                    if (feedType == 0) {
                        // No animations configured, use default calculation
                        feedType = FeedState::Calculate(targetState, vampireStage);
                        SKSE::log::info("Vampire stage: {} | FeedType: {} (state={} + stage={})",
                            vampireStage, feedType, targetState, vampireStage);
                    }
                }

                // Set graph variables on both player and target for OAR conditions
                // These are read by OAR's HasGraphVariable condition
                // Only works if user has Behavior Data Injector installed
                SetFeedGraphVars(player, feedType);
                SetFeedGraphVars(currentTarget_, feedType);

                // InitiateVampireFeedPackage handles the animation
                SKSE::log::info("Calling InitiateVampireFeedPackage...");
                player->InitiateVampireFeedPackage(currentTarget_, furnitureRef);

                // Call the Papyrus VampireFeed() function to update vampire status
                // This detects if mod uses VampireFeed() or VampireFeed(Actor)
                auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
                if (vampireQuest) {
                    PapyrusCall::CallVampireFeed(vampireQuest, currentTarget_);
                } else {
                    SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
                }
            }
            break;
        case SkyPromptAPI::PromptEventType::kDeclined:
            SKSE::log::debug("Feed DECLINED");
            break;
        case SkyPromptAPI::PromptEventType::kDown:
            SKSE::log::debug("Feed button DOWN");
            break;
        case SkyPromptAPI::PromptEventType::kUp:
            SKSE::log::debug("Feed button UP");
            break;
        case SkyPromptAPI::PromptEventType::kTimingOut:
            // Prompt is about to expire - resend to refresh it
            if (currentTarget_ && g_clientID != 0) {
                SkyPromptAPI::SendPrompt(this, g_clientID);
                SKSE::log::debug("Prompt timing out - refreshed");
            }
            break;
        default:
            break;
        }
    }

    void SetTarget(RE::Actor* target) {
        currentTarget_ = target;
        if (target) {
            prompts_[0] = SkyPromptAPI::Prompt(
                "Feed",
                1,  // eventID
                1,  // actionID
                SkyPromptAPI::PromptType::kSinglePress,
                target->GetFormID(),
                feedButtons_
            );
        }
    }

    RE::Actor* GetTarget() const {
        return currentTarget_;
    }

    // Set graph variables for OAR animation conditions
    // These only work if user has Behavior Data Injector installed
    // If not installed, SetGraphVariable silently fails - safe to call always
    static void SetFeedGraphVars(RE::Actor* actor, int feedType) {
        if (!actor) return;

        // Set bool flag
        bool success = actor->SetGraphVariableBool(GraphVars::IsSkyPromptFeeding, true);
        if (success) {
            SKSE::log::debug("Set graph var {} = true on {}",
                GraphVars::IsSkyPromptFeeding, actor->GetName());
        }

        // Set composite feed type (TargetState * 10 + HungerStage)
        success = actor->SetGraphVariableInt(GraphVars::SkyPromptFeedType, feedType);
        if (success) {
            SKSE::log::debug("Set graph var {} = {} on {}",
                GraphVars::SkyPromptFeedType, feedType, actor->GetName());
        }
    }

    static void ClearFeedGraphVars(RE::Actor* actor) {
        if (!actor) return;

        actor->SetGraphVariableBool(GraphVars::IsSkyPromptFeeding, false);
        actor->SetGraphVariableInt(GraphVars::SkyPromptFeedType, 0);
        SKSE::log::debug("Cleared feed graph vars on {}", actor->GetName());
    }

    // Returns true if target should be excluded from feeding (no prompt shown)
    static bool IsExcluded(RE::Actor* actor) {
        if (!actor) return true;

        auto* settings = Settings::GetSingleton();
        if (!settings->General.EnableMod) return true;

        // Check if player or target is in a paired animation
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && PairedAnimation::IsInPairedAnimation(player)) {
            SKSE::log::debug("Excluded: player is in paired animation");
            return true;
        }
        if (PairedAnimation::IsInPairedAnimation(actor)) {
            SKSE::log::debug("Excluded: {} is in paired animation", actor->GetName());
            return true;
        }

        // Check common filters first (level, keywords)
        if (IsExcludedByFilters(actor)) return true;

        bool isInCombat = actor->IsInCombat();

        SKSE::log::debug("IsExcluded check: {} | InCombat: {}", actor->GetName(), isInCombat);

        if (isInCombat) {
            return IsExcludedCombat(actor);
        } else {
            return IsExcludedNonCombat(actor);
        }
    }

private:
    // Check if actor has a keyword by editor ID
    static bool HasKeyword(RE::Actor* actor, const std::string& keywordEditorID) {
        if (!actor) return false;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return false;

        auto* keyword = dataHandler->LookupForm<RE::BGSKeyword>(RE::FormID(0), keywordEditorID);
        if (!keyword) {
            // Try to find by iterating keywords (slower but works for all keywords)
            for (const auto& kw : dataHandler->GetFormArray<RE::BGSKeyword>()) {
                if (kw && kw->GetFormEditorID() == keywordEditorID) {
                    keyword = kw;
                    break;
                }
            }
        }

        if (!keyword) return false;
        return actor->HasKeyword(keyword);
    }

    // Check level and keyword filters (applies to both combat and non-combat)
    static bool IsExcludedByFilters(RE::Actor* actor) {
        if (!actor) return true;

        auto* settings = Settings::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();

        // Dead check - skip dead actors
        if (settings->Filtering.ExcludeDead) {
            if (actor->IsDead()) {
                SKSE::log::debug("Excluded: {} - actor is dead", actor->GetName());
                return true;
            }
        }

        // Scene check - skip actors in dialogues/scripted events
        if (settings->Filtering.ExcludeInScene) {
            if (actor->GetCurrentScene() != nullptr) {
                SKSE::log::debug("Excluded: {} - currently in a scene", actor->GetName());
                return true;
            }
        }

        // Level check
        if (settings->Filtering.EnableLevelCheck && player) {
            int playerLevel = player->GetLevel();
            int targetLevel = actor->GetLevel();
            int levelDiff = targetLevel - playerLevel;

            if (levelDiff > settings->Filtering.MaxLevelDifference) {
                SKSE::log::debug("Excluded: {} - level {} is {} above player level {} (max diff: {})",
                    actor->GetName(), targetLevel, levelDiff, playerLevel, settings->Filtering.MaxLevelDifference);
                return true;
            }
        }

        // Exclude keywords check (if has any excluded keyword, exclude)
        for (const auto& kw : settings->Filtering.ExcludeKeywords) {
            if (HasKeyword(actor, kw)) {
                SKSE::log::debug("Excluded: {} - has excluded keyword '{}'", actor->GetName(), kw);
                return true;
            }
        }

        // Include keywords check (if list not empty, must have at least one)
        if (!settings->Filtering.IncludeKeywords.empty()) {
            bool hasIncludedKeyword = false;
            for (const auto& kw : settings->Filtering.IncludeKeywords) {
                if (HasKeyword(actor, kw)) {
                    hasIncludedKeyword = true;
                    break;
                }
            }
            if (!hasIncludedKeyword) {
                SKSE::log::debug("Excluded: {} - missing required include keywords", actor->GetName());
                return true;
            }
        }

        return false;
    }

    // Exclusion checks for targets in combat
    static bool IsExcludedCombat(RE::Actor* actor) {
        if (!actor) return true;

        auto* settings = Settings::GetSingleton();

        // Combat feeding disabled
        if (!settings->Combat.Enabled) {
            SKSE::log::debug("Combat path: {} - excluded (combat feeding disabled)", actor->GetName());
            return true;
        }

        // Check if low health is required
        if (settings->Combat.RequireLowHealth) {
            auto* avOwner = actor->AsActorValueOwner();
            if (avOwner) {
                float currentHealth = avOwner->GetActorValue(RE::ActorValue::kHealth);
                float maxHealth = avOwner->GetPermanentActorValue(RE::ActorValue::kHealth);
                float healthPct = (maxHealth > 0.0f) ? (currentHealth / maxHealth) : 1.0f;
                if (healthPct > settings->Combat.LowHealthThreshold) {
                    SKSE::log::debug("Combat path: {} - excluded (health {:.1f}% > {:.1f}%)",
                        actor->GetName(), healthPct * 100, settings->Combat.LowHealthThreshold * 100);
                    return true;
                }
            }
        }

        SKSE::log::debug("Combat path: {} - not excluded", actor->GetName());
        return false;
    }

    // Exclusion checks for targets not in combat
    static bool IsExcludedNonCombat(RE::Actor* actor) {
        if (!actor) return true;

        auto* settings = Settings::GetSingleton();

        bool isSitting = TargetState::IsSitting(actor);
        bool isSleeping = TargetState::IsSleeping(actor);
        bool isStanding = TargetState::IsStanding(actor);
        auto furnitureType = TargetState::GetActorFurnitureType(actor);

        SKSE::log::debug("NonCombat path: {} | Standing: {} | Sitting: {} | Sleeping: {} | FurnitureType: {}",
            actor->GetName(), isStanding, isSitting, isSleeping,
            TargetState::FurnitureTypeToString(furnitureType));

        // Check standing
        if (isStanding && !settings->NonCombat.AllowStanding) {
            SKSE::log::debug("Excluded: {} is standing (disabled in settings)", actor->GetName());
            return true;
        }

        // Check sleeping
        if (isSleeping && !settings->NonCombat.AllowSleeping) {
            SKSE::log::debug("Excluded: {} is sleeping (disabled in settings)", actor->GetName());
            return true;
        }

        // Check sitting in chair
        if (isSitting && furnitureType == TargetState::FurnitureType::kChair) {
            if (!settings->NonCombat.AllowSittingChair) {
                SKSE::log::debug("Excluded: {} is sitting in chair (disabled in settings)", actor->GetName());
                return true;
            }
        }

        return false;
    }

private:
    VampireFeedSink() {
        prompts_[0] = SkyPromptAPI::Prompt(
            "Feed",
            1,  // eventID
            1,  // actionID
            SkyPromptAPI::PromptType::kSinglePress,
            0,
            feedButtons_
        );
    }

    // G key (0x22) + Gamepad A (0x1000)
    static constexpr std::array<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>, 2> feedButtons_ = {{
        {RE::INPUT_DEVICE::kKeyboard, 0x22},
        {RE::INPUT_DEVICE::kGamepad, 0x1000}
    }};

    std::array<SkyPromptAPI::Prompt, 1> prompts_;
    mutable RE::Actor* currentTarget_ = nullptr;
};
