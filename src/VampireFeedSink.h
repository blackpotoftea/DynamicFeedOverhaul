#pragma once
#include "SkyPrompt/API.hpp"
#include "Settings.h"
#include "TargetState.h"
#include "util.h"

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

                if (TargetState::IsSleeping(currentTarget_)) {
                    SKSE::log::info("Target is SLEEPING - using bed feed");
                } else if (TargetState::IsSitting(currentTarget_)) {
                    SKSE::log::info("Target is SITTING - using seated feed");
                } else if (TargetState::IsStanding(currentTarget_)) {
                    SKSE::log::info("Target is STANDING - using standing feed");
                }

                // InitiateVampireFeedPackage takes furniture ref as second param for bed feeding
                SKSE::log::info("Calling InitiateVampireFeedPackage...");
                player->InitiateVampireFeedPackage(currentTarget_, furnitureRef);
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

    // Returns true if target should be excluded from feeding (no prompt shown)
    static bool IsExcluded(RE::Actor* actor) {
        if (!actor) return true;

        auto* settings = Settings::GetSingleton();
        if (!settings->General.EnableMod) return true;

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
