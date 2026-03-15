#pragma once
#include "Settings.h"
#include "feed/TargetState.h"
#include "utils/FormUtils.h"
#include "utils/AnimUtil.h"
#include "integration/PoiseIntegration.h"

// Feed target filtering/exclusion logic
namespace FeedFiltering {

    // Check level and keyword filters (applies to both combat and non-combat)
    inline bool IsExcludedByFilters(RE::Actor* actor) {
        if (!actor) return true;

        auto* settings = Settings::GetSingleton();

        // Dead check - skip dead actors unless AllowRecentlyDead is enabled
        if (actor->IsDead()) {
            // Werewolves can always feed on corpses (no time/feed limits)
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && TargetState::IsWerewolf(player)) {
                SKSE::log::debug("Allowed: {} - werewolf can devour any corpse", actor->GetName());
                return false;
            }

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
                // Dead targets that pass the above checks skip all other filters
                // (no level check, no scene check, no keyword restrictions)
                return false;
            } else if (settings->Filtering.ExcludeDead) {
                SKSE::log::debug("Excluded: {} - actor is dead", actor->GetName());
                return true;
            }
        }

        // Scene check - skip actors in dialogues/scripted events (living only)
        if (settings->Filtering.ExcludeInScene) {
            if (actor->GetCurrentScene() != nullptr) {
                SKSE::log::debug("Excluded: {} - currently in a scene", actor->GetName());
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

        // Exclude actor IDs check (if matches any excluded base form ID, exclude)
        for (const auto& actorID : settings->Filtering.ExcludeActorIDs) {
            if (FormUtils::MatchesBaseFormID(actor, actorID)) {
                SKSE::log::debug("Excluded: {} - matches excluded actor ID '{}'", actor->GetName(), actorID);
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

        // Allow feeding on staggered targets in combat (bypasses health check)
        if (settings->Combat.AllowStaggered && TargetState::IsStaggered(actor)) {
            // Check level requirements for stagger feeding (unless poise mod bypasses it)
            bool poiseBypassesLevel = settings->Integration.PoiseIgnoresLevelCheck && PoiseIntegration::IsAvailable();

            if (!poiseBypassesLevel && settings->Combat.StaggerRequireLowerLevel) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player) {
                    int playerLevel = player->GetLevel();
                    int targetLevel = actor->GetLevel();
                    int maxAllowedLevel = playerLevel - settings->Combat.StaggerMaxLevelDifference;

                    // Target must be lower level than player by at least StaggerMaxLevelDifference
                    // e.g., player level 20, StaggerMaxLevelDifference 10 -> target must be level 10 or lower
                    if (targetLevel > maxAllowedLevel) {
                        SKSE::log::debug("Combat path: {} - excluded (staggered but level {} > max allowed {}, player {} - diff {})",
                            actor->GetName(), targetLevel, maxAllowedLevel, playerLevel, settings->Combat.StaggerMaxLevelDifference);
                        return true;
                    }
                }
            } else if (poiseBypassesLevel) {
                SKSE::log::debug("Combat path: {} - stagger level check bypassed (poise mod detected)", actor->GetName());
            }

            SKSE::log::debug("Combat path: {} - allowed (target is staggered)", actor->GetName());
            return false;
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

        // Dead actors skip all non-combat restrictions (level, posture, etc.)
        // They were already validated in IsExcludedByFilters for death time and feed limits
        if (actor->IsDead()) {
            SKSE::log::debug("NonCombat: {} - skipping checks (dead)", actor->GetName());
            return false;
        }

        auto* settings = Settings::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();

        // Level check (only applies outside combat, living targets only)
        if (settings->NonCombat.EnableLevelCheck && player) {
            int playerLevel = player->GetLevel();
            int targetLevel = actor->GetLevel();
            int levelDiff = targetLevel - playerLevel;

            if (levelDiff > settings->NonCombat.MaxLevelDifference) {
                SKSE::log::debug("NonCombat: {} - excluded (level {} is {} above player level {}, max diff: {})",
                    actor->GetName(), targetLevel, levelDiff, playerLevel, settings->NonCombat.MaxLevelDifference);
                return true;
            }
        }

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
