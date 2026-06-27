#include "integration/FeedIntegration.h"
#include "Settings.h"
#include "papyrus/PapyrusCall.h"
#include "integration/VampireIntegrationUtils.h"
#include "feed/TargetState.h"
#include "utils/AnimUtil.h"

namespace FeedIntegration {
    void Run(RE::Actor* callbackTarget, bool isLethal, bool hasOARAnimation) {
        if (!callbackTarget) {
            SKSE::log::warn("RunFeedIntegration: target is null");
            return;
        }

        SKSE::log::info("RunFeedIntegration: target={}, lethal={}, hasOAR={}",
            callbackTarget->GetName(), isLethal, hasOARAnimation);

        auto* player = RE::PlayerCharacter::GetSingleton();

        // Send custom DAO_VampireFeed event with attacker and target (always send our custom event)
        if (player) {
            PapyrusCall::SendDAO_VampireFeedEvent(player, callbackTarget);
        }

        // Only call vampire script if NOT a werewolf
        if (player && !TargetState::IsWerewolf(player)) {
            auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
            if (vampireQuest) {
                // If lethal, the kill move animation handles the kill - don't double-kill in integration
                bool animationHandlesKill = isLethal;
                // DISABLE TMP to test
                PapyrusCall::CallVampireFeed(vampireQuest, callbackTarget, isLethal, animationHandlesKill);
            } else {
                SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
            }
        }
        // Werewolf corpse feeding
        else if (player && TargetState::IsWerewolf(player) && callbackTarget && callbackTarget->IsDead()) {
            SKSE::log::info("Werewolf corpse feed - applying effects");

            // 1. Apply PlayerWerewolfFeedVictimSpell to player
            auto* feedSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("PlayerWerewolfFeedVictimSpell");
            if (feedSpell) {
                VampireIntegrationUtils::CastSpell(feedSpell, player, player);
                SKSE::log::debug("Applied PlayerWerewolfFeedVictimSpell");
            } else {
                SKSE::log::warn("PlayerWerewolfFeedVictimSpell not found");
            }

            // 2. Call PlayerWerewolfChangeScript.Feed()
            auto* werewolfQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("PlayerWerewolfQuest");
            if (werewolfQuest) {
                VampireIntegrationUtils::CallPapyrusMethod(werewolfQuest, "PlayerWerewolfChangeScript", "Feed");
                SKSE::log::debug("Called PlayerWerewolfChangeScript.Feed()");
            } else {
                SKSE::log::warn("PlayerWerewolfQuest not found");
            }
        }

        // Integration-specific post-feed handling
        PapyrusCall::VampireIntegration integration = PapyrusCall::DetectVampireIntegration();
        switch (integration) {
            case PapyrusCall::VampireIntegration::Sacrosanct:
                SKSE::log::debug("Post-feed: Sacrosanct integration active - letting Sacrosanct handle kill");
                // Sacrosanct handles killing via ProcessFeed call above
                // No additional action needed
                break;

            case PapyrusCall::VampireIntegration::BetterVampires:
                SKSE::log::debug("Post-feed: Better Vampires integration active");
                // Better Vampires may handle killing differently
                // No additional action needed for now
                break;

            case PapyrusCall::VampireIntegration::Vanilla:
                SKSE::log::debug("Post-feed: Vanilla vampire system active");
                // Send the vanilla OnVampireFeed event - only relevant for the vanilla
                // vampire system; modded overhauls drive feeding through their own
                // ProcessFeed/VampireFeed dispatch in PapyrusCall::CallVampireFeed.
                PapyrusCall::SendOnVampireFeedEvent(callbackTarget);
                [[fallthrough]];

            default:
                // Manual-kill fallback for Vanilla and any integration without its own
                // kill handling (e.g. Sacrilege, which falls through here). Only kill if:
                // 1. User wants lethal feed
                // 2. NO OAR combat animation found (if OAR anim exists, kill is baked in)
                if (isLethal && callbackTarget && !hasOARAnimation) {
                    SKSE::log::info("No OAR animation found - manually killing target after animation");
                    AnimUtil::KillTarget(callbackTarget);
                } else if (isLethal && hasOARAnimation) {
                    SKSE::log::info("OAR combat animation found - letting animation handle kill");
                }
                break;
        }
    }
}
