#include "integration/FeedIntegration.h"
#include "Settings.h"
#include "papyrus/PapyrusCall.h"
#include "integration/VampireIntegrationUtils.h"
#include "feed/TargetState.h"
#include "utils/AnimUtil.h"

namespace {
    // C++ replication of PlayerVampireQuestScript.VampireFeed(), with the parts that
    // interrupt an in-progress paired feed animation deliberately omitted:
    //   - Utility.Wait(2.0) + the image-space crossfade  -> blocking + purely cosmetic
    //   - VampireFeedMessage.Show()                       -> opens a menu, which ejects
    //                                                        the player from the feed idle
    //   - the VampireProgression EquipSpell swaps         -> equip anim breaks the idle
    //   - Game.IncrementStat("Necks Bitten")              -> cosmetic, no clean native
    //   - VampirePCFaction / crime-faction cleanup        -> no-op here: the matching
    //                                                        "hated stage-4 vampire" code
    //                                                        is commented out in the vanilla
    //                                                        script, so there is nothing to undo
    // What remains is the state that actually matters for the feed loop: reset hunger,
    // regress to vampire stage 1, reset the feed timer, and swap stage abilities back.
    void ApplyVanillaVampireFeed(RE::Actor* player, RE::TESQuest* vampireQuest) {
        if (!player || !vampireQuest) return;

        constexpr const char* kScript = "PlayerVampireQuestScript";

        // Hunger meter -> "just fed" (VampireFeedReady is 0-3; 0 = freshly fed)
        if (auto* feedReady = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireFeedReady")) {
            feedReady->value = 0.0f;
        }

        // Regress vampire stage to 1
        VampireIntegrationUtils::SetScriptPropertyInt(vampireQuest, kScript, "VampireStatus", 1);

        // Reset the feed timer to now so progression (OnUpdateGameTime) restarts from a
        // full stomach instead of immediately re-advancing.
        if (auto* gameDays = RE::TESForm::LookupByEditorID<RE::TESGlobal>("GameDaysPassed")) {
            VampireIntegrationUtils::SetScriptPropertyFloat(vampireQuest, kScript, "LastFeedTime", gameDays->value);
        }

        // Swap vampire abilities back to stage 1 (mirrors VampireProgression(player, 1)
        // minus the EquipSpell calls). Spell forms are read straight from the quest
        // script's own properties, so there is no EditorID guessing.
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return;
        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, vampireQuest);
        RE::BSTSmartPointer<RE::BSScript::Object> obj;
        if (!vm->FindBoundObject(handle, kScript, obj) || !obj) {
            SKSE::log::warn("ApplyVanillaVampireFeed: {} not bound - abilities not regressed", kScript);
            return;
        }

        auto getSpell = [&](const char* prop) -> RE::SpellItem* {
            auto* var = obj->GetProperty(prop);
            if (!var || !var->IsObject()) return nullptr;
            return var->Unpack<RE::SpellItem*>();
        };
        auto addSpell = [&](const char* prop) { if (auto* s = getSpell(prop)) player->AddSpell(s); };
        auto removeSpell = [&](const char* prop) { if (auto* s = getSpell(prop)) player->RemoveSpell(s); };

        addSpell("ABVampireSkills");
        addSpell("ABVampireSkills02");
        removeSpell("AbVampire04");
        removeSpell("AbVampire02");
        removeSpell("AbVampire03");
        removeSpell("AbVampire04b");
        removeSpell("AbVampire02b");
        removeSpell("AbVampire03b");
        addSpell("AbVampire01");
        addSpell("AbVampire01b");

        addSpell("VampireDrain01");
        removeSpell("VampireDrain04");
        removeSpell("VampireDrain02");
        removeSpell("VampireDrain03");

        removeSpell("VampireRaiseThrall04");
        removeSpell("VampireRaiseThrall02");
        removeSpell("VampireRaiseThrall03");
        addSpell("VampireRaiseThrall01");

        removeSpell("VampireSunDamage04");
        removeSpell("VampireSunDamage02");
        removeSpell("VampireSunDamage03");
        addSpell("VampireSunDamage01");

        removeSpell("VampireCharm");
        removeSpell("VampireInvisibilityPC");

        SKSE::log::info("ApplyVanillaVampireFeed: hunger reset, regressed to vampire stage 1");
    }
}

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

        PapyrusCall::VampireIntegration integration = PapyrusCall::DetectVampireIntegration();

        // Werewolf corpse feeding is its own path - not a vampire feed, so it does not run
        // the vampire-integration switch below.
        if (player && TargetState::IsWerewolf(player)) {
            if (callbackTarget->IsDead()) {
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
            return;
        }

        if (!player) return;

        auto* vampireQuest = PapyrusCall::GetPlayerVampireQuest();
        if (!vampireQuest) {
            SKSE::log::warn("PlayerVampireQuest not found - vampire status won't update");
            return;
        }

        // Each integration owns its COMPLETE handling here: the feed call plus the post-feed
        // work (vanilla event + manual kill). The 4th CallVampireFeed arg (animationHandlesKill
        // = isLethal) tells the integration the kill-move animation does the kill on a lethal feed.
        switch (integration) {
            case PapyrusCall::VampireIntegration::Sacrosanct:
                // Sacrosanct's ProcessFeed performs the feed and owns the kill.
                PapyrusCall::CallVampireFeed(vampireQuest, callbackTarget, isLethal, isLethal);
                break;

            case PapyrusCall::VampireIntegration::BetterVampires:
                // Better Vampires' VampireFeed(Actor) owns the feed and its own kill handling.
                PapyrusCall::CallVampireFeed(vampireQuest, callbackTarget, isLethal, isLethal);
                break;

            case PapyrusCall::VampireIntegration::Sacrilege:
                // Sacrilege's ProcessFeed only handles the kill in some cases; manual fallback otherwise.
                PapyrusCall::CallVampireFeed(vampireQuest, callbackTarget, isLethal, isLethal);
                if (isLethal && !hasOARAnimation) {
                    SKSE::log::info("No OAR animation found - manually killing target after animation");
                    AnimUtil::KillTarget(callbackTarget);
                }
                break;

            case PapyrusCall::VampireIntegration::Vanilla:
                // Vanilla: replicate VampireFeed() in C++ (no Papyrus VampireFeed dispatch, so the
                // paired feed idle is never interrupted), fire the vanilla OnVampireFeed event,
                // then the manual-kill fallback.
                // ApplyVanillaVampireFeed(player, vampireQuest);

                bool animationHandlesKill = isLethal;
                // DISABLE TMP to test
                PapyrusCall::CallVampireFeed(vampireQuest, callbackTarget, isLethal, animationHandlesKill);
                PapyrusCall::SendOnVampireFeedEvent(callbackTarget);
                if (isLethal && !hasOARAnimation) {
                    SKSE::log::info("No OAR animation found - manually killing target after animation");
                    AnimUtil::KillTarget(callbackTarget);
                }
                break;
        }
    }
}
