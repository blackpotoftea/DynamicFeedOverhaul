#include "PCH.h"
#include "SacrilegeIntegration.h"

/*
 * =============================================================================
 * SACRILEGE PROCESSFEED - C++ IMPLEMENTATION
 * =============================================================================
 *
 * Based on SQL_FeedManager_Script.ProcessFeed() from Sacrilege mod.
 * Sacrilege is a minimalistic vampire mod - simpler than Sacrosanct.
 *
 * PAPYRUS FUNCTION SIGNATURE:
 *   ProcessFeed(Actor akTarget, Bool akIsLethal, Bool akIsSleeping,
 *               Bool akVanillaFeedAnimation, Bool akDisable)
 *
 * =============================================================================
 * IMPLEMENTATION STEPS
 * =============================================================================
 *
 * STEP | FEATURE                          | STATUS
 * -----|----------------------------------|----------------------------------
 *  1   | Set BlockFeeding flag            | ✅ DONE
 *  2   | StartVampireFeed                 | SKIP - causes AI-driven state
 *  3   | DLC1VampireTurn.PlayerBitesMe    | ✅ DONE
 *  4   | Play feed sound                  | ✅ DONE
 *  5   | PlayerVampireQuest.VampireFeed() | ✅ DONE
 *  6   | Lethal kill                      | ✅ DONE
 *  7   | Restore Health (+ level scaling) | ✅ DONE
 *  8   | Fountain of Blood perk (3x)      | ✅ DONE
 *  9   | Dunmer racial proc               | ✅ DONE
 * 10   | Clear BlockFeeding flag          | ✅ DONE
 * 11   | Lifeblood progression            | ✅ DONE
 * 12   | Imperial racial (potion chance)  | ✅ DONE
 * 13   | Age advancement                  | ✅ DONE
 * 14   | Destruction XP                   | ✅ DONE
 *
 * =============================================================================
 */

namespace SacrilegeIntegration {

    namespace {
        bool g_initialized = false;
        bool g_available = false;

        // Globals
        RE::TESGlobal* g_blockFeedingFlag = nullptr;
        RE::TESGlobal* g_restoreStatsBase = nullptr;
        RE::TESGlobal* g_restoreStatsLevel = nullptr;
        RE::TESGlobal* g_xpFeedBase = nullptr;
        RE::TESGlobal* g_xpFeedLevel = nullptr;
        RE::TESGlobal* g_xpDrainBase = nullptr;
        RE::TESGlobal* g_xpDrainLevel = nullptr;
        RE::TESGlobal* g_ageBonusFeed = nullptr;
        RE::TESGlobal* g_ageBonusDrain = nullptr;
        RE::TESGlobal* g_bloodPointsIncrement = nullptr;

        // DLC1 globals (shared with vanilla)
        RE::TESGlobal* g_dlc1BloodPoints = nullptr;
        RE::TESGlobal* g_dlc1NextPerk = nullptr;
        RE::TESGlobal* g_dlc1PerkPoints = nullptr;
        RE::TESGlobal* g_dlc1TotalPerksEarned = nullptr;
        RE::TESGlobal* g_dlc1MaxPerks = nullptr;

        // Quests
        RE::TESQuest* g_sacrilegeQuest = nullptr;
        RE::TESQuest* g_playerVampireQuest = nullptr;
        RE::TESQuest* g_dlc1VampireTurnQuest = nullptr;

        // Sounds
        RE::TESSound* g_feedSound = nullptr;

        // Messages
        RE::BGSMessage* g_dlc1BloodPointsMsg = nullptr;
        RE::BGSMessage* g_dlc1PerkEarnedMsg = nullptr;

        // Perks
        RE::BGSPerk* g_fountainOfBloodPerk = nullptr;

        // Spells
        RE::SpellItem* g_vampireLordPower = nullptr;
        RE::SpellItem* g_dunmerProcSpell = nullptr;

        // Effects
        RE::EffectSetting* g_dunmerEffect = nullptr;
        RE::EffectSetting* g_imperialEffect = nullptr;

        // Keywords
        RE::BGSKeyword* g_actorTypeNPC = nullptr;

        // Items
        RE::AlchemyItem* g_advanceAgePotion = nullptr;
        RE::AlchemyItem* g_dlc1BloodPotion = nullptr;

        // Helper: Play sound on actor
        void PlaySound(RE::TESSound* sound, RE::Actor* target) {
            if (!sound || !target) return;

            auto* descriptor = sound->descriptor;
            if (!descriptor) return;

            auto* audioManager = RE::BSAudioManager::GetSingleton();
            if (!audioManager) return;

            RE::BSSoundHandle soundHandle;
            audioManager->BuildSoundDataFromDescriptor(soundHandle, descriptor);

            if (soundHandle.IsValid()) {
                soundHandle.SetObjectToFollow(target->Get3D());
                soundHandle.Play();
            }
        }

        // Helper: Show message
        void ShowMessage(RE::BGSMessage* message) {
            if (!message) return;
            RE::BSString result;
            message->GetDescription(result, nullptr);
            RE::DebugNotification(result.c_str());
        }

        // Helper: Call Papyrus method (no args)
        bool CallPapyrusMethod(RE::TESQuest* quest, const char* scriptName, const char* funcName) {
            if (!quest) return false;
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            auto* args = RE::MakeFunctionArguments();

            class EmptyCallback : public RE::BSScript::IStackCallbackFunctor {
            public:
                void operator()(RE::BSScript::Variable) override {}
                bool CanSave() const override { return false; }
                void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            };
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

            bool result = vm->DispatchMethodCall(handle, scriptName, funcName, args, callback);
            if (!result) delete args;
            return result;
        }

        // Helper: Call PlayerBitesMe on DLC1VampireTurn
        bool CallPlayerBitesMe(RE::Actor* target) {
            if (!target || !g_dlc1VampireTurnQuest) return false;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
                RE::TESQuest::FORMTYPE, g_dlc1VampireTurnQuest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            auto* args = RE::MakeFunctionArguments(std::move(target));

            class EmptyCallback : public RE::BSScript::IStackCallbackFunctor {
            public:
                void operator()(RE::BSScript::Variable) override {}
                bool CanSave() const override { return false; }
                void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            };
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

            bool result = vm->DispatchMethodCall(handle, "DLC1VampireTurnScript", "PlayerBitesMe", args, callback);
            if (!result) delete args;
            return result;
        }

        // Helper: Call AdvanceAge on PlayerVampireQuest
        bool CallAdvanceAge(RE::Actor* player, float amount) {
            if (!player || !g_playerVampireQuest) return false;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
                RE::TESQuest::FORMTYPE, g_playerVampireQuest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            auto* args = RE::MakeFunctionArguments(std::move(player), std::move(amount));

            class EmptyCallback : public RE::BSScript::IStackCallbackFunctor {
            public:
                void operator()(RE::BSScript::Variable) override {}
                bool CanSave() const override { return false; }
                void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            };
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

            bool result = vm->DispatchMethodCall(handle, "PlayerVampireQuestScript", "AdvanceAge", args, callback);
            if (!result) delete args;
            return result;
        }

        // Helper: Cast spell
        void CastSpell(RE::SpellItem* spell, RE::Actor* caster, RE::Actor* target) {
            if (!spell || !caster) return;
            auto* magicCaster = caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
            if (magicCaster) {
                magicCaster->CastSpellImmediate(spell, false, target, 1.0f, false, 0.0f, nullptr);
            }
        }

        // Helper: Check if actor has magic effect
        bool HasMagicEffect(RE::Actor* actor, RE::EffectSetting* effect) {
            if (!actor || !effect) return false;
            auto* magicTarget = actor->AsMagicTarget();
            if (!magicTarget) return false;

            auto* activeEffects = magicTarget->GetActiveEffectList();
            if (!activeEffects) return false;

            for (auto* activeEffect : *activeEffects) {
                if (activeEffect && activeEffect->GetBaseObject() == effect) {
                    return true;
                }
            }
            return false;
        }

        // Lifeblood progression (Sacrilege version of Vampire Lord XP)
        void ProgressLifeblood(RE::Actor* player) {
            if (!g_dlc1BloodPoints || !g_dlc1NextPerk || !g_dlc1PerkPoints ||
                !g_dlc1TotalPerksEarned || !g_dlc1MaxPerks) return;

            g_dlc1BloodPoints->value += 1.0f;

            if (g_dlc1TotalPerksEarned->value < g_dlc1MaxPerks->value) {
                ShowMessage(g_dlc1BloodPointsMsg);

                if (g_dlc1BloodPoints->value >= g_dlc1NextPerk->value) {
                    g_dlc1BloodPoints->value -= g_dlc1NextPerk->value;
                    g_dlc1PerkPoints->value += 1.0f;
                    g_dlc1TotalPerksEarned->value += 1.0f;

                    // Sacrilege uses configurable increment
                    float increment = g_bloodPointsIncrement ? g_bloodPointsIncrement->value : 1.0f;
                    g_dlc1NextPerk->value += increment;

                    ShowMessage(g_dlc1PerkEarnedMsg);
                    SKSE::log::info("SacrilegeIntegration: Earned Vampire Lord perk point!");
                }

                // Update VampirePerks AV for progress bar
                float progress = (g_dlc1BloodPoints->value / g_dlc1NextPerk->value) * 100.0f;
                player->AsActorValueOwner()->SetActorValue(RE::ActorValue::kVampirePerks, progress);
            }

            // Sacrilege calls AdvanceAge after Lifeblood
            float ageDelta = 24.0f;  // SQL_AgeDelta property default
            CallAdvanceAge(player, ageDelta);
        }
    }

    bool Initialize() {
        if (g_initialized) {
            return g_available;
        }
        g_initialized = true;

        SKSE::log::info("SacrilegeIntegration: Initializing...");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::warn("SacrilegeIntegration: DataHandler not available");
            return false;
        }

        // Check for Sacrilege ESP
        bool hasSacrilege = dataHandler->LookupModByName("Sacrilege - Minimalistic Vampires of Skyrim.esp") != nullptr;
        if (!hasSacrilege) {
            SKSE::log::info("SacrilegeIntegration: Sacrilege not installed");
            g_available = false;
            return false;
        }

        SKSE::log::info("SacrilegeIntegration: Sacrilege ESP detected, looking up forms...");

        // Globals - Sacrilege specific
        g_blockFeedingFlag = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_Flag_BlockFeeding");
        g_restoreStatsBase = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_RestoreStatsOnFeed_Base");
        g_restoreStatsLevel = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_RestoreStatsOnFeed_Level");
        g_xpFeedBase = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_XP_Feed_Base");
        g_xpFeedLevel = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_XP_Feed_Level");
        g_xpDrainBase = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_XP_Drain_Base");
        g_xpDrainLevel = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_XP_Drain_Level");
        g_ageBonusFeed = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_Age_Bonus_Feed");
        g_ageBonusDrain = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_Age_Bonus_Drain");
        g_bloodPointsIncrement = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SQL_Mechanics_Global_BloodPoints_IncrementPerLevel");

        // DLC1 globals (shared)
        g_dlc1BloodPoints = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampireBloodPoints");
        g_dlc1NextPerk = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampireNextPerk");
        g_dlc1PerkPoints = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampirePerkPoints");
        g_dlc1TotalPerksEarned = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampireTotalPerksEarned");
        g_dlc1MaxPerks = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampireMaxPerks");

        // Quests
        g_sacrilegeQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SQL_FeedManager_Quest");
        g_playerVampireQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("PlayerVampireQuest");
        g_dlc1VampireTurnQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("DLC1VampireTurn");

        // Sounds
        g_feedSound = RE::TESForm::LookupByEditorID<RE::TESSound>("SQL_Mechanics_Marker_FeedSound");

        // Messages
        g_dlc1BloodPointsMsg = RE::TESForm::LookupByEditorID<RE::BGSMessage>("DLC1BloodPointsMsg");
        g_dlc1PerkEarnedMsg = RE::TESForm::LookupByEditorID<RE::BGSMessage>("DLC1VampirePerkEarned");

        // Perks
        g_fountainOfBloodPerk = RE::TESForm::LookupByEditorID<RE::BGSPerk>("SQL_PerkTree_Perk_VampireLord_Melee_01_FountainOfBlood");

        // Spells
        g_vampireLordPower = RE::TESForm::LookupByEditorID<RE::SpellItem>("SQL_VampireLord_Power_Spell_VampireLord");
        g_dunmerProcSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SQL_Racial_Spell_Dunmer_Proc");

        // Effects
        g_dunmerEffect = RE::TESForm::LookupByEditorID<RE::EffectSetting>("SQL_Racial_Effect_Dunmer_Ab");
        g_imperialEffect = RE::TESForm::LookupByEditorID<RE::EffectSetting>("SQL_Racial_Effect_Imperial_Ab");

        // Keywords
        g_actorTypeNPC = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");

        // Items
        g_advanceAgePotion = RE::TESForm::LookupByEditorID<RE::AlchemyItem>("SQL_Potion_Potion_AdvanceAge");
        g_dlc1BloodPotion = RE::TESForm::LookupByEditorID<RE::AlchemyItem>("DLC1BloodPotion");

        g_available = true;
        SKSE::log::debug("SacrilegeIntegration: Initialized successfully");

        // Globals - Sacrilege specific
        SKSE::log::debug("  SQL_Mechanics_Global_Flag_BlockFeeding: {}", g_blockFeedingFlag ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_RestoreStatsOnFeed_Base: {}", g_restoreStatsBase ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_RestoreStatsOnFeed_Level: {}", g_restoreStatsLevel ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_XP_Feed_Base: {}", g_xpFeedBase ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_XP_Feed_Level: {}", g_xpFeedLevel ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_XP_Drain_Base: {}", g_xpDrainBase ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_XP_Drain_Level: {}", g_xpDrainLevel ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_Age_Bonus_Feed: {}", g_ageBonusFeed ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_Age_Bonus_Drain: {}", g_ageBonusDrain ? "found" : "missing");
        SKSE::log::debug("  SQL_Mechanics_Global_BloodPoints_IncrementPerLevel: {}", g_bloodPointsIncrement ? "found" : "missing");

        // DLC1 globals
        SKSE::log::debug("  DLC1VampireBloodPoints: {}", g_dlc1BloodPoints ? "found" : "missing");
        SKSE::log::debug("  DLC1VampireNextPerk: {}", g_dlc1NextPerk ? "found" : "missing");
        SKSE::log::debug("  DLC1VampirePerkPoints: {}", g_dlc1PerkPoints ? "found" : "missing");
        SKSE::log::debug("  DLC1VampireTotalPerksEarned: {}", g_dlc1TotalPerksEarned ? "found" : "missing");
        SKSE::log::debug("  DLC1VampireMaxPerks: {}", g_dlc1MaxPerks ? "found" : "missing");

        // Quests
        SKSE::log::debug("  SQL_FeedManager_Quest: {}", g_sacrilegeQuest ? "found" : "missing");
        SKSE::log::debug("  PlayerVampireQuest: {}", g_playerVampireQuest ? "found" : "missing");
        SKSE::log::debug("  DLC1VampireTurn: {}", g_dlc1VampireTurnQuest ? "found" : "missing");

        // Sounds
        SKSE::log::debug("  SQL_Mechanics_Marker_FeedSound: {}", g_feedSound ? "found" : "missing");

        // Messages
        SKSE::log::debug("  DLC1BloodPointsMsg: {}", g_dlc1BloodPointsMsg ? "found" : "missing");
        SKSE::log::debug("  DLC1VampirePerkEarned: {}", g_dlc1PerkEarnedMsg ? "found" : "missing");

        // Perks
        SKSE::log::debug("  SQL_PerkTree_Perk_VampireLord_Melee_01_FountainOfBlood: {}", g_fountainOfBloodPerk ? "found" : "missing");

        // Spells
        SKSE::log::debug("  SQL_VampireLord_Power_Spell_VampireLord: {}", g_vampireLordPower ? "found" : "missing");
        SKSE::log::debug("  SQL_Racial_Spell_Dunmer_Proc: {}", g_dunmerProcSpell ? "found" : "missing");

        // Effects
        SKSE::log::debug("  SQL_Racial_Effect_Dunmer_Ab: {}", g_dunmerEffect ? "found" : "missing");
        SKSE::log::debug("  SQL_Racial_Effect_Imperial_Ab: {}", g_imperialEffect ? "found" : "missing");

        // Keywords
        SKSE::log::debug("  ActorTypeNPC: {}", g_actorTypeNPC ? "found" : "missing");

        // Items
        SKSE::log::debug("  SQL_Potion_Potion_AdvanceAge: {}", g_advanceAgePotion ? "found" : "missing");
        SKSE::log::debug("  DLC1BloodPotion: {}", g_dlc1BloodPotion ? "found" : "missing");

        return true;
    }

    bool IsAvailable() {
        if (!g_initialized) {
            Initialize();
        }
        return g_available;
    }

    bool ProcessFeed(const FeedContext& context) {
        if (!context.target) {
            SKSE::log::error("SacrilegeIntegration::ProcessFeed: target is null");
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("SacrilegeIntegration::ProcessFeed: player is null");
            return false;
        }

        SKSE::log::info("SacrilegeIntegration::ProcessFeed: target={}, lethal={}, combat={}, sleeping={}",
            context.target->GetName(), context.isLethal, context.isCombatFeed, context.isSleeping);

        int targetLevel = context.target->GetLevel();
        if (targetLevel > 50) targetLevel = 50;  // Cap at 50 like Papyrus

        bool targetIsNPC = g_actorTypeNPC && context.target->HasKeyword(g_actorTypeNPC);

        // === STEP 1: Set BlockFeeding flag ===
        if (g_blockFeedingFlag) {
            g_blockFeedingFlag->value = 1.0f;
        }

        // === STEP 2: StartVampireFeed - SKIP (causes AI-driven state) ===

        // === STEP 3: DLC1VampireTurn.PlayerBitesMe ===
        CallPlayerBitesMe(context.target);

        // === STEP 4: Play feed sound ===
        if (g_feedSound) {
            PlaySound(g_feedSound, context.target);
        }

        // === STEP 5: PlayerVampireQuest.VampireFeed() ===
        if (g_playerVampireQuest) {
            CallPapyrusMethod(g_playerVampireQuest, "PlayerVampireQuestScript", "VampireFeed");
        }

        // === STEP 6: Lethal kill ===
        // Skip if animation handles the kill (lethal idle/OAR animation has kill baked in)
        if (context.isLethal && !context.animationHandlesKill && !context.target->IsDead()) {
            context.target->KillImpl(player, 1000.0f, true, true);
            SKSE::log::info("SacrilegeIntegration: Killed target");
        } else if (context.isLethal && context.animationHandlesKill) {
            SKSE::log::info("SacrilegeIntegration: Skipping kill - animation handles it");
        }

        // === STEP 7-8: Restore Health (with Fountain of Blood perk check) ===
        float restoreBase = g_restoreStatsBase ? g_restoreStatsBase->value : 50.0f;
        float restoreLevel = g_restoreStatsLevel ? g_restoreStatsLevel->value : 2.0f;
        float restoreAmount = restoreBase + static_cast<float>(targetLevel) * restoreLevel;

        auto* avOwner = player->AsActorValueOwner();
        if (g_fountainOfBloodPerk && player->HasPerk(g_fountainOfBloodPerk) && context.isLethal) {
            // Fountain of Blood: 3x restore to all stats on lethal
            avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, restoreAmount * 3.0f);
            avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, restoreAmount * 3.0f);
            avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, restoreAmount * 3.0f);
            SKSE::log::info("SacrilegeIntegration: Fountain of Blood - restored {:.0f} x3 to all stats", restoreAmount);
        } else {
            // Normal: only restore health
            avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, restoreAmount);
            SKSE::log::info("SacrilegeIntegration: Restored {:.0f} Health", restoreAmount);
        }

        // === STEP 9: Dunmer racial proc ===
        if (g_dunmerEffect && HasMagicEffect(player, g_dunmerEffect) && g_dunmerProcSpell) {
            CastSpell(g_dunmerProcSpell, player, context.target);
            SKSE::log::info("SacrilegeIntegration: Applied Dunmer racial ability");
        }

        // === STEP 10: Clear BlockFeeding flag ===
        if (g_blockFeedingFlag) {
            g_blockFeedingFlag->value = 0.0f;
        }

        // === STEP 11: Lifeblood progression (lethal + has VL power) ===
        if (context.isLethal && context.target->IsDead()) {
            if (g_vampireLordPower && player->HasSpell(g_vampireLordPower)) {
                ProgressLifeblood(player);
            }

            // === STEP 12: Imperial racial (blood potion chance) ===
            if (g_imperialEffect && HasMagicEffect(player, g_imperialEffect)) {
                float roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
                if (roll < 0.25f) {
                    // 25% chance for Advance Age potion
                    if (g_advanceAgePotion) {
                        player->AddObjectToContainer(g_advanceAgePotion, nullptr, 1, nullptr);
                        SKSE::log::info("SacrilegeIntegration: Imperial racial - added Advance Age potion");
                    }
                } else {
                    // 75% chance for Blood Potion
                    if (g_dlc1BloodPotion) {
                        player->AddObjectToContainer(g_dlc1BloodPotion, nullptr, 1, nullptr);
                        SKSE::log::info("SacrilegeIntegration: Imperial racial - added Blood Potion");
                    }
                }
            }
        }

        // === STEP 13-14: Age advancement and Destruction XP ===
        if (context.isLethal) {
            float ageBonus = g_ageBonusDrain ? g_ageBonusDrain->value : 1.0f;
            float xpBase = g_xpDrainBase ? g_xpDrainBase->value : 10.0f;
            float xpLevel = g_xpDrainLevel ? g_xpDrainLevel->value : 1.0f;

            if (targetIsNPC) {
                CallAdvanceAge(player, ageBonus);
                player->AddSkillExperience(RE::ActorValue::kDestruction, xpBase + static_cast<float>(targetLevel) * xpLevel);
            } else {
                // Non-NPC: 25% of normal values
                CallAdvanceAge(player, ageBonus * 0.25f);
                player->AddSkillExperience(RE::ActorValue::kDestruction, (xpBase + static_cast<float>(targetLevel) * xpLevel) * 0.25f);
            }
            SKSE::log::info("SacrilegeIntegration: Lethal feed - advanced age and added Destruction XP");
        } else {
            // Non-lethal feed
            float ageBonus = g_ageBonusFeed ? g_ageBonusFeed->value : 0.5f;
            float xpBase = g_xpFeedBase ? g_xpFeedBase->value : 5.0f;
            float xpLevel = g_xpFeedLevel ? g_xpFeedLevel->value : 0.5f;

            CallAdvanceAge(player, ageBonus);
            player->AddSkillExperience(RE::ActorValue::kDestruction, xpBase + static_cast<float>(targetLevel) * xpLevel);
            SKSE::log::info("SacrilegeIntegration: Non-lethal feed - advanced age and added Destruction XP");
        }

        SKSE::log::info("SacrilegeIntegration::ProcessFeed: Complete");
        return true;
    }
}
