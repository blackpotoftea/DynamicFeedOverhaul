#pragma once
#include "Settings.h"
#include "feed/TargetState.h"
#include "utils/FormUtils.h"
#include "utils/AnimUtil.h"

// Feed target filtering/exclusion logic
namespace FeedFiltering {

    // Check level and keyword filters (applies to both combat and non-combat)
    inline bool IsExcludedByFilters(RE::Actor* actor) {
        if (!actor) return true;

        auto* settings = Settings::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();

        // Dead check - skip dead actors unless AllowRecentlyDead is enabled
        if (actor->IsDead()) {
            if (settings->Filtering.AllowRecentlyDead) {
                // Check death time - returns -1 if invalid (pre-placed corpse or no AI process)
                float hoursSinceDeath = AnimUtil::GetHoursSinceDeath(actor);
                if (hoursSinceDeath < 0.0f) {
                    SKSE::log::debug("Excluded: {} - invalid corpse (no death time, pre-placed or skeleton)",
                        actor->GetName());
                    return true;
                }
                // Check if recently dead
                if (hoursSinceDeath > settings->Filtering.MaxDeadHours) {
                    SKSE::log::debug("Excluded: {} - dead too long ({:.2f}h > {:.2f}h limit)",
                        actor->GetName(), hoursSinceDeath, settings->Filtering.MaxDeadHours);
                    return true;
                }
                // Check if feed limit exceeded
                if (AnimUtil::HasExceededDeadFeedLimit(actor, settings->Filtering.MaxDeadFeeds)) {
                    SKSE::log::debug("Excluded: {} - exceeded dead feed limit ({})",
                        actor->GetName(), settings->Filtering.MaxDeadFeeds);
                    return true;
                }
                SKSE::log::debug("Allowed: {} - recently dead ({:.2f}h/{:.2f}h, {}/{} feeds)",
                    actor->GetName(), hoursSinceDeath, settings->Filtering.MaxDeadHours,
                    AnimUtil::GetDeadFeedCount(actor), settings->Filtering.MaxDeadFeeds);
                // Continue to other checks - don't exclude
            } else if (settings->Filtering.ExcludeDead) {
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
            if (FormUtils::HasKeywordByEditorID(actor, kw)) {
                SKSE::log::debug("Excluded: {} - has excluded keyword '{}'", actor->GetName(), kw);
                return true;
            }
        }

        // Include keywords check (if list not empty, must have at least one)
        if (!settings->Filtering.IncludeKeywords.empty()) {
            bool hasIncludedKeyword = false;
            for (const auto& kw : settings->Filtering.IncludeKeywords) {
                if (FormUtils::HasKeywordByEditorID(actor, kw)) {
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
    inline bool IsExcludedCombat(RE::Actor* actor) {
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
    inline bool IsExcludedNonCombat(RE::Actor* actor) {
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
}
