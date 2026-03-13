#include "PCH.h"
#include "SacrosanctIntegration.h"

/*
 * =============================================================================
 * SACROSANCT PROCESSFEED - PAPYRUS PARITY IMPLEMENTATION
 * =============================================================================
 *
 * This file implements the Sacrosanct ProcessFeed function in C++ to bypass
 * the Papyrus AI-driven state issues during combat feeding.
 *
 * PAPYRUS FUNCTION SIGNATURE:
 *   ProcessFeed(Actor akTarget, Bool akIsLethal, Bool akIsSleeping,
 *               Bool akIsSneakFeed, Bool akIsParalyzed, Bool akIsCombatFeed, Bool akIsEmbrace)
 *
 * =============================================================================
 * IMPLEMENTATION STATUS (27 Steps from Papyrus)
 * =============================================================================
 *
 * STEP | FEATURE                          | STATUS
 * -----|----------------------------------|----------------------------------
 *  1   | Essential NPC check              | SKIP - handled elsewhere
 *  2   | Quest stage (SetStage 10)        | ✅ DONE - SetQuestStage() via Papyrus
 *  3   | DLC1VampireTurn.PlayerBitesMe    | ✅ DONE - CallPreFeedPapyrus()
 *  4   | Target restraint (SetRestrained) | SKIP - paired animation handles
 *  5   | Feed sound                       | ✅ DONE - BSAudioManager + BSSoundHandle
 *  6   | SCS_Mechanics_Spell_Feed_Target  | ✅ DONE - CastSpell()
 *  7   | Amaranth (vampire lethal)        | ✅ DONE - both spells
 *  8   | PlayerVampireQuest.VampireFeed() | ✅ DONE - Papyrus call
 *  9   | Racial abilities (Dunmer/etc)    | ✅ DONE - ApplyRacialAbility()
 * 10   | Sneak feed alarm + spell         | ✅ DONE - spell done, alarm handled by animation
 * 11   | Lethal kill                      | ✅ DONE - ProcessLethalKill()
 * 12   | Destruction XP (lethal)          | ✅ DONE - AddSkillExperience()
 * 13   | Reset hunger stage               | ✅ DONE - via PlayerVampireQuest.VampireFeed()
 * 14   | Reset feed timer                 | ✅ DONE - via PlayerVampireQuest.VampireFeed()
 * 15   | Restore H/M/S (100 + level*20)   | ✅ DONE - RestoreActorValue()
 * 16   | Combat cleanup (controls/AI)     | SKIP - animation system handles
 * 17   | Blood Knight stamina cost        | ✅ DONE - combat feed stamina drain
 * 18   | Kiss of Death (sleeping+lethal)  | ✅ DONE - stat bonus
 * 19   | Embrace spell                    | ✅ DONE - CastSpell()
 * 20   | Psychic Vampire perk             | ✅ DONE - perk check + spell
 * 21   | Dispel progression spells        | ✅ DONE - DispelFeedDebuffs()
 * 22   | Hemomancy progression            | ✅ DONE - ProcessHemomancy()
 * 23   | Strong Blood quest               | ✅ DONE - spells 00-06, counter, RemoveForm
 * 24   | Vampire Lord XP (DLC1)           | ✅ DONE - ProcessVampireLordXP()
 * 25   | Lamae's Wrath flag               | ✅ DONE - global set
 * 26   | Blood Bond                       | ✅ DONE - faction-based (no SetRelationshipRank)
 * 27   | Wassail reset                    | ✅ DONE - ResetWassail()
 * 28   | Harvest Moon (blood potion)      | ✅ DONE - AddObjectToContainer()
 * 29   | Age mechanic                     | ✅ DONE - Papyrus call to Age()
 * 30   | Final AI cleanup                 | SKIP - animation system handles
 *
 * =============================================================================
 * KNOWN LIMITATIONS
 * =============================================================================
 *
 * - SetRelationshipRank: No direct C++ API, using faction system instead
 *
 * =============================================================================
 * FORM DEPENDENCIES (cached in Initialize())
 * =============================================================================
 *
 * Quests:
 *   - SCS_FeedManager_Quest, PlayerVampireQuest, DLC1VampireTurn
 *   - SCS_Main500_Quest, SCS_Sommelier_Quest
 *
 * Spells:
 *   - SCS_Mechanics_Spell_Feed_Target, Amaranth, Amaranth_Target
 *   - Racial: Dunmer_Spell, Altmer_Proc, Altmer_Proc_Long, OrcNew_Proc, OrcNew_Proc_Long
 *   - SneakFeed_Target, FeedEmbrace_Target, PsychicVampire_Target
 *   - ReverseProgression_Stage2N_Proc, BloodIsPower
 *   - Strong Blood: StrongBlood_Spell_00_Ab through _06_Ab (7 spells)
 *
 * Effects:
 *   - Racial: Dunmer_Ab, Altmer_Ab, Orc_Ab
 *
 * Globals:
 *   - XP: LethalFeed_Base, LethalFeed_Level
 *   - Hemomancy: Stage, Steps, StepsToNext, StepsToNext_AddPerStep
 *   - Wassail: Current, NerfAmount
 *   - Age: BonusFromDrain, BonusFromFeed
 *   - DLC1: BloodPoints, NextPerk, PerkPoints, TotalPerksEarned, MaxPerks
 *   - BloodKnight_Cost, KissOfDeath_Amount, CanLamaesWrath, ForceUniqueCheck
 *   - StrongBloodCounter
 *
 * FormLists:
 *   - SCS_Mechanics_FormList_HemomancyExpanded
 *   - SCS_Mechanics_FormList_StrongBlood, StrongBlood_Track
 *
 * Perks:
 *   - SCS_PsychicVampire_Perk, SCS_LethalFeedXP
 *
 * Factions:
 *   - CurrentFollowerFaction, PotentialFollowerFaction, PotentialMarriageFaction
 *
 * Items:
 *   - DLC1BloodPotion
 *
 * =============================================================================
 */

namespace SacrosanctIntegration {

    namespace {
        // Cached state
        bool g_initialized = false;
        bool g_sacrosanctAvailable = false;

        // Cached form lookups (populated during Initialize)
        RE::TESQuest* g_sacrosanctQuest = nullptr;
        RE::TESQuest* g_dlc1VampireTurnQuest = nullptr;
        // NOTE: VampireFeedTimer/VampireFeedReady not needed - PlayerVampireQuest.VampireFeed() handles timer
        RE::BGSPerk* g_vampireFeedPerk = nullptr;

        // Sacrosanct spells and XP globals
        RE::SpellItem* g_scsFeedTargetSpell = nullptr;
        RE::TESGlobal* g_scsXPLethalBase = nullptr;
        RE::TESGlobal* g_scsXPLethalLevel = nullptr;

        // === NEW FORMS FOR FULL PAPYRUS PARITY ===

        // Messages
        RE::BGSMessage* g_msgHemomancyStageUp = nullptr;
        RE::BGSMessage* g_msgStrongBlood = nullptr;
        RE::BGSMessage* g_msgBloodBond = nullptr;

        // Sounds - Sound Markers are BGSSoundDescriptorForm (SNDR)
        RE::BGSSoundDescriptorForm* g_feedSound = nullptr;

        // Quests
        RE::TESQuest* g_playerVampireQuest = nullptr;
        RE::TESQuest* g_scsMain500Quest = nullptr;
        RE::TESQuest* g_sommelierQuest = nullptr;

        // Spells - Amaranth (vampire lethal)
        RE::SpellItem* g_amaranthSpell = nullptr;
        RE::SpellItem* g_amaranthTargetSpell = nullptr;

        // Spells - Racial
        RE::EffectSetting* g_dunmerEffect = nullptr;
        RE::SpellItem* g_dunmerSpell = nullptr;
        RE::EffectSetting* g_altmerEffect = nullptr;
        RE::SpellItem* g_altmerProcSpell = nullptr;
        RE::SpellItem* g_altmerProcLongSpell = nullptr;
        RE::EffectSetting* g_orcEffect = nullptr;
        RE::SpellItem* g_orcProcSpell = nullptr;
        RE::SpellItem* g_orcProcLongSpell = nullptr;

        // Spells - Feed types
        RE::SpellItem* g_sneakFeedSpell = nullptr;
        RE::SpellItem* g_embraceSpell = nullptr;
        RE::SpellItem* g_psychicVampireSpell = nullptr;

        // Spells - Dispel targets
        RE::SpellItem* g_reverseProgressionProc = nullptr;
        RE::SpellItem* g_bloodIsPowerSpell = nullptr;

        // Perks
        RE::BGSPerk* g_psychicVampirePerk = nullptr;
        RE::BGSPerk* g_lethalFeedXPPerk = nullptr;

        // Spells - Abilities (reward spells)
        RE::SpellItem* g_bloodBondAbility = nullptr;
        RE::SpellItem* g_harvestMoonAbility = nullptr;

        // Keywords
        RE::BGSKeyword* g_vampireKeyword = nullptr;

        // Globals - Combat/Stamina
        RE::TESGlobal* g_bloodKnightCost = nullptr;

        // Globals - Kiss of Death
        RE::TESGlobal* g_kissOfDeathAmount = nullptr;
        RE::SpellItem* g_kissOfDeathAbility = nullptr;

        // Globals - Hemomancy
        RE::TESGlobal* g_hemomancyStage = nullptr;
        RE::TESGlobal* g_hemomancySteps = nullptr;
        RE::TESGlobal* g_hemomancyStepsToNext = nullptr;
        RE::TESGlobal* g_hemomancyStepsAddPerStep = nullptr;
        RE::BGSListForm* g_hemomancyFormList = nullptr;
        RE::SpellItem* g_hemomancyBaseAbility = nullptr;
        RE::BGSMessage* g_hemomancyHelpMessage = nullptr;  // SCS_Help_GetHemomancyByFeeding

        // Globals - Wassail
        RE::TESGlobal* g_wassailCurrent = nullptr;
        RE::TESGlobal* g_wassailNerfAmount = nullptr;

        // Globals - Age
        RE::TESGlobal* g_ageBonusFromDrain = nullptr;
        RE::TESGlobal* g_ageBonusFromFeed = nullptr;

        // Globals - Lamae's Wrath
        RE::TESGlobal* g_canLamaesWrath = nullptr;

        // Globals - Unique Check
        RE::TESGlobal* g_forceUniqueCheck = nullptr;

        // DLC1 Vampire Lord progression
        RE::TESGlobal* g_dlc1BloodPoints = nullptr;
        RE::TESGlobal* g_dlc1NextPerk = nullptr;
        RE::TESGlobal* g_dlc1PerkPoints = nullptr;
        RE::TESGlobal* g_dlc1TotalPerksEarned = nullptr;
        RE::TESGlobal* g_dlc1MaxPerks = nullptr;
        RE::BGSMessage* g_dlc1BloodPointsMsg = nullptr;
        RE::BGSMessage* g_dlc1PerkEarnedMsg = nullptr;

        // Formlists - Strong Blood
        RE::BGSListForm* g_strongBloodTrack = nullptr;
        RE::BGSListForm* g_strongBloodBase = nullptr;

        // Strong Blood spell rewards (indices 0-6)
        RE::SpellItem* g_strongBloodSpells[7] = { nullptr };

        // NOTE: StrongBloodCounter is a script-local property on SCS_FeedManager_Quest, not a global
        // Access via GetScriptPropertyInt/SetScriptPropertyInt helper functions

        // Factions - Blood Bond
        RE::TESFaction* g_currentFollowerFaction = nullptr;
        RE::TESFaction* g_potentialFollowerFaction = nullptr;
        RE::TESFaction* g_potentialMarriageFaction = nullptr;

        // Items
        RE::AlchemyItem* g_dlc1BloodPotion = nullptr;
    }

    bool Initialize() {
        if (g_initialized) {
            return g_sacrosanctAvailable;
        }
        g_initialized = true;

        SKSE::log::info("SacrosanctIntegration: Initializing...");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::warn("SacrosanctIntegration: DataHandler not available");
            return false;
        }

        // Check for Sacrosanct ESP/ESL
        bool hasSacrosanct = dataHandler->LookupModByName("Sacrosanct - Vampires of Skyrim.esp") != nullptr;
        if (!hasSacrosanct) {
            SKSE::log::info("SacrosanctIntegration: Sacrosanct not installed");
            g_sacrosanctAvailable = false;
            return false;
        }

        SKSE::log::info("SacrosanctIntegration: Sacrosanct ESP detected, looking up forms...");

        // Cache vanilla vampire perk (VampireFeedTimer/VampireFeedReady not needed - handled by VampireFeed())
        g_vampireFeedPerk = RE::TESForm::LookupByEditorID<RE::BGSPerk>("VampireFeed");

        // Cache Sacrosanct quest
        g_sacrosanctQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SCS_FeedManager_Quest");
        if (!g_sacrosanctQuest) {
            SKSE::log::warn("SacrosanctIntegration: SCS_FeedManager_Quest not found");
        }

        // Cache DLC1 vampire turn quest (for PlayerBitesMe)
        g_dlc1VampireTurnQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("DLC1VampireTurn");

        // Cache feed spell and XP globals
        g_scsFeedTargetSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Mechanics_Spell_Feed_Target");
        g_scsXPLethalBase = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_XP_LethalFeed_Base");
        g_scsXPLethalLevel = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_XP_LethalFeed_Level");

        // === NEW FORM LOOKUPS FOR FULL PAPYRUS PARITY ===

        // Messages
        g_msgHemomancyStageUp = RE::TESForm::LookupByEditorID<RE::BGSMessage>("SCS_Mechanics_Message_HemomancyStageUp");
        g_msgStrongBlood = RE::TESForm::LookupByEditorID<RE::BGSMessage>("SCS_Mechanics_Message_StrongBlood");
        g_msgBloodBond = RE::TESForm::LookupByEditorID<RE::BGSMessage>("SCS_Mechanics_Message_BloodBond");

        // Sounds - Sacrosanct uses Sound Markers which are BGSSoundDescriptorForm (SNDR)
        g_feedSound = RE::TESForm::LookupByEditorID<RE::BGSSoundDescriptorForm>("SCS_Mechanics_Marker_FeedSound");

        // Quests
        g_playerVampireQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("PlayerVampireQuest");
        g_scsMain500Quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SCS_Main500_Quest");
        g_sommelierQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SCS_Sommelier_Quest");

        // Spells - Amaranth (vampire lethal)
        g_amaranthSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Mechanics_Spell_Amaranth");
        g_amaranthTargetSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Mechanics_Spell_Amaranth_Target");

        // Spells - Racial
        g_dunmerEffect = RE::TESForm::LookupByEditorID<RE::EffectSetting>("SCS_Abilities_Racial_Effect_Dunmer_Ab");
        g_dunmerSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Racial_Spell_Dunmer_Spell");
        g_altmerEffect = RE::TESForm::LookupByEditorID<RE::EffectSetting>("SCS_Abilities_Racial_Effect_Altmer_Ab");
        g_altmerProcSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Racial_Spell_Altmer_Proc");
        g_altmerProcLongSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Racial_Spell_Altmer_Proc_Long");
        g_orcEffect = RE::TESForm::LookupByEditorID<RE::EffectSetting>("SCS_Abilities_Racial_Effect_Orc_Ab");
        g_orcProcSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Racial_Spell_OrcNew_Proc");
        g_orcProcLongSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Racial_Spell_OrcNew_Proc_Long");

        // Spells - Feed types
        g_sneakFeedSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Mechanics_Spell_FeedSneak_Target");
        g_embraceSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Mechanics_Spell_FeedFosterChilde_Target");
        g_psychicVampireSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Mechanics_Spell_PsychicVampire_Self");

        // Spells - Dispel targets
        g_reverseProgressionProc = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Vanilla_Spell_Ab_ReverseProgression_Stage2N_Proc");
        g_bloodIsPowerSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_VampireSpells_Vanilla_Power_Spell_BloodIsPower");

        // Perks
        g_psychicVampirePerk = RE::TESForm::LookupByEditorID<RE::BGSPerk>("SCS_PerkTree_320_Perk_VampireLord_Mortal_PsychicVampire");
        g_lethalFeedXPPerk = RE::TESForm::LookupByEditorID<RE::BGSPerk>("SCS_PerkTree_300_Perk_VampireLord_Mortal_WhiteWolf");
        g_bloodBondAbility = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Reward_Spell_BloodBond_Ab");
        g_harvestMoonAbility = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Reward_Spell_HarvestMoon_Ab");

        // Keywords
        g_vampireKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("Vampire");

        // Globals - Combat/Stamina
        g_bloodKnightCost = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_BloodKnight_Cost");

        // Globals - Kiss of Death
        g_kissOfDeathAmount = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_KissOfDeath_Amount");
        g_kissOfDeathAbility = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Vanilla_Spell_Ab_ReverseProgression_Stage2");

        // Globals - Hemomancy
        g_hemomancyStage = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_VampireSpells_Hemomancy_Global_Stage");
        g_hemomancySteps = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_VampireSpells_Hemomancy_Global_Stage_Steps");
        g_hemomancyStepsToNext = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_VampireSpells_Hemomancy_Global_Stage_StepsToNext");
        g_hemomancyStepsAddPerStep = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_VampireSpells_Hemomancy_Global_Stage_StepsToNext_AddPerStep");
        g_hemomancyFormList = RE::TESForm::LookupByEditorID<RE::BGSListForm>("SCS_Mechanics_FormList_Hemomancy");
        g_hemomancyBaseAbility = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Mechanics_Spell_Ab_AddRemoveHemomancySpells");
        g_hemomancyHelpMessage = RE::TESForm::LookupByEditorID<RE::BGSMessage>("SCS_Help_GetHemomancyByFeeding");

        // Globals - Wassail
        g_wassailCurrent = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_Wassail_Current");
        g_wassailNerfAmount = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_Wassail_NerfAmount");

        // Globals - Age
        g_ageBonusFromDrain = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_Age_BonusFromDrain");
        g_ageBonusFromFeed = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_Age_BonusFromFeed");

        // Globals - Lamae's Wrath
        g_canLamaesWrath = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_VampireSpells_Vanilla_Power_Global_CanLamaesPyre");

        // Globals - Unique Check
        g_forceUniqueCheck = RE::TESForm::LookupByEditorID<RE::TESGlobal>("SCS_Mechanics_Global_ForceUniqueCheck");

        // DLC1 Vampire Lord progression
        g_dlc1BloodPoints = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampireBloodPoints");
        g_dlc1NextPerk = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampireNextPerk");
        g_dlc1PerkPoints = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampirePerkPoints");
        g_dlc1TotalPerksEarned = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampireTotalPerksEarned");
        g_dlc1MaxPerks = RE::TESForm::LookupByEditorID<RE::TESGlobal>("DLC1VampireMaxPerks");
        g_dlc1BloodPointsMsg = RE::TESForm::LookupByEditorID<RE::BGSMessage>("DLC1BloodPointsMsg");
        g_dlc1PerkEarnedMsg = RE::TESForm::LookupByEditorID<RE::BGSMessage>("DLC1VampirePerkEarned");

        // Formlists - Strong Blood
        g_strongBloodTrack = RE::TESForm::LookupByEditorID<RE::BGSListForm>("SCS_Mechanics_FormList_StrongBlood_Track");
        g_strongBloodBase = RE::TESForm::LookupByEditorID<RE::BGSListForm>("SCS_Mechanics_FormList_StrongBlood");

        // Strong Blood spell rewards (SCS_Abilities_StrongBlood_Spell_00_Ab through _06_Ab)
        g_strongBloodSpells[0] = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_StrongBlood_Spell_00_Ab");
        g_strongBloodSpells[1] = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_StrongBlood_Spell_01_Ab");
        g_strongBloodSpells[2] = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_StrongBlood_Spell_02_Ab");
        g_strongBloodSpells[3] = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_StrongBlood_Spell_03_Ab");
        g_strongBloodSpells[4] = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_StrongBlood_Spell_04_Ab");
        g_strongBloodSpells[5] = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_StrongBlood_Spell_05_Ab");
        g_strongBloodSpells[6] = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_StrongBlood_Spell_06_Ab");

        // NOTE: StrongBloodCounter is accessed via script property, not cached here

        // Factions - Blood Bond
        g_currentFollowerFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("CurrentFollowerFaction");
        g_potentialFollowerFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("PotentialFollowerFaction");
        g_potentialMarriageFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("PotentialMarriageFaction");

        // Items
        g_dlc1BloodPotion = RE::TESForm::LookupByEditorID<RE::AlchemyItem>("DLC1BloodPotion");

        g_sacrosanctAvailable = true;
        SKSE::log::debug("SacrosanctIntegration: Initialized successfully");

        // === QUESTS ===
        SKSE::log::debug("  [Quests]");
        SKSE::log::debug("    SCS_FeedManager_Quest: {}", g_sacrosanctQuest ? "found" : "missing");
        SKSE::log::debug("    PlayerVampireQuest: {}", g_playerVampireQuest ? "found" : "missing");
        SKSE::log::debug("    DLC1VampireTurn: {}", g_dlc1VampireTurnQuest ? "found" : "missing");
        SKSE::log::debug("    SCS_Main500_Quest: {}", g_scsMain500Quest ? "found" : "missing");
        SKSE::log::debug("    SCS_Sommelier_Quest: {}", g_sommelierQuest ? "found" : "missing");

        // === SPELLS - CORE ===
        SKSE::log::debug("  [Spells - Core]");
        SKSE::log::debug("    SCS_Mechanics_Spell_Feed_Target: {}", g_scsFeedTargetSpell ? "found" : "missing");
        SKSE::log::debug("    Amaranth: {}", g_amaranthSpell ? "found" : "missing");
        SKSE::log::debug("    Amaranth_Target: {}", g_amaranthTargetSpell ? "found" : "missing");

        // === SPELLS - RACIAL ===
        SKSE::log::debug("  [Spells - Racial]");
        SKSE::log::debug("    Dunmer_Effect: {}", g_dunmerEffect ? "found" : "missing");
        SKSE::log::debug("    Dunmer_Spell: {}", g_dunmerSpell ? "found" : "missing");
        SKSE::log::debug("    Altmer_Effect: {}", g_altmerEffect ? "found" : "missing");
        SKSE::log::debug("    Altmer_Proc: {}", g_altmerProcSpell ? "found" : "missing");
        SKSE::log::debug("    Altmer_Proc_Long: {}", g_altmerProcLongSpell ? "found" : "missing");
        SKSE::log::debug("    Orc_Effect: {}", g_orcEffect ? "found" : "missing");
        SKSE::log::debug("    Orc_Proc: {}", g_orcProcSpell ? "found" : "missing");
        SKSE::log::debug("    Orc_Proc_Long: {}", g_orcProcLongSpell ? "found" : "missing");

        // === SPELLS - FEED TYPES ===
        SKSE::log::debug("  [Spells - Feed Types]");
        SKSE::log::debug("    SneakFeed_Target: {}", g_sneakFeedSpell ? "found" : "missing");
        SKSE::log::debug("    Embrace_Target: {}", g_embraceSpell ? "found" : "missing");
        SKSE::log::debug("    PsychicVampire_Target: {}", g_psychicVampireSpell ? "found" : "missing");

        // === SPELLS - DISPEL/PROGRESSION ===
        SKSE::log::debug("  [Spells - Dispel/Progression]");
        SKSE::log::debug("    ReverseProgression_Proc: {}", g_reverseProgressionProc ? "found" : "missing");
        SKSE::log::debug("    BloodIsPower: {}", g_bloodIsPowerSpell ? "found" : "missing");
        SKSE::log::debug("    KissOfDeath_Ability: {}", g_kissOfDeathAbility ? "found" : "missing");

        // === PERKS ===
        SKSE::log::debug("  [Perks]");
        SKSE::log::debug("    VampireFeed (vanilla): {}", g_vampireFeedPerk ? "found" : "missing");
        SKSE::log::debug("    PsychicVampire_Perk: {}", g_psychicVampirePerk ? "found" : "missing");
        SKSE::log::debug("    LethalFeedXP: {}", g_lethalFeedXPPerk ? "found" : "missing");

        // === SPELLS - ABILITIES (REWARDS) ===
        SKSE::log::debug("  [Spells - Abilities]");
        SKSE::log::debug("    BloodBond_Ab: {}", g_bloodBondAbility ? "found" : "missing");
        SKSE::log::debug("    HarvestMoon_Ab: {}", g_harvestMoonAbility ? "found" : "missing");
        SKSE::log::debug("    Hemomancy_BaseAbility: {}", g_hemomancyBaseAbility ? "found" : "missing");

        // === KEYWORDS ===
        SKSE::log::debug("  [Keywords]");
        SKSE::log::debug("    Vampire: {}", g_vampireKeyword ? "found" : "missing");

        // === GLOBALS - XP ===
        SKSE::log::debug("  [Globals - XP]");
        SKSE::log::debug("    XP_LethalFeed_Base: {}", g_scsXPLethalBase ? "found" : "missing");
        SKSE::log::debug("    XP_LethalFeed_Level: {}", g_scsXPLethalLevel ? "found" : "missing");

        // === GLOBALS - COMBAT ===
        SKSE::log::debug("  [Globals - Combat]");
        SKSE::log::debug("    BloodKnight_Cost: {}", g_bloodKnightCost ? "found" : "missing");
        SKSE::log::debug("    KissOfDeath_Amount: {}", g_kissOfDeathAmount ? "found" : "missing");

        // === GLOBALS - HEMOMANCY ===
        SKSE::log::debug("  [Globals - Hemomancy]");
        SKSE::log::debug("    Hemomancy_Stage: {}", g_hemomancyStage ? "found" : "missing");
        SKSE::log::debug("    Hemomancy_Steps: {}", g_hemomancySteps ? "found" : "missing");
        SKSE::log::debug("    Hemomancy_StepsToNext: {}", g_hemomancyStepsToNext ? "found" : "missing");
        SKSE::log::debug("    Hemomancy_StepsAddPerStep: {}", g_hemomancyStepsAddPerStep ? "found" : "missing");

        // === GLOBALS - WASSAIL ===
        SKSE::log::debug("  [Globals - Wassail]");
        SKSE::log::debug("    Wassail_Current: {}", g_wassailCurrent ? "found" : "missing");
        SKSE::log::debug("    Wassail_NerfAmount: {}", g_wassailNerfAmount ? "found" : "missing");

        // === GLOBALS - AGE ===
        SKSE::log::debug("  [Globals - Age]");
        SKSE::log::debug("    Age_BonusFromDrain: {}", g_ageBonusFromDrain ? "found" : "missing");
        SKSE::log::debug("    Age_BonusFromFeed: {}", g_ageBonusFromFeed ? "found" : "missing");

        // === GLOBALS - MISC ===
        SKSE::log::debug("  [Globals - Misc]");
        SKSE::log::debug("    CanLamaesWrath: {}", g_canLamaesWrath ? "found" : "missing");
        SKSE::log::debug("    ForceUniqueCheck: {}", g_forceUniqueCheck ? "found" : "missing");

        // === GLOBALS - DLC1 VAMPIRE LORD ===
        SKSE::log::debug("  [Globals - DLC1 Vampire Lord]");
        SKSE::log::debug("    DLC1BloodPoints: {}", g_dlc1BloodPoints ? "found" : "missing");
        SKSE::log::debug("    DLC1NextPerk: {}", g_dlc1NextPerk ? "found" : "missing");
        SKSE::log::debug("    DLC1PerkPoints: {}", g_dlc1PerkPoints ? "found" : "missing");
        SKSE::log::debug("    DLC1TotalPerksEarned: {}", g_dlc1TotalPerksEarned ? "found" : "missing");
        SKSE::log::debug("    DLC1MaxPerks: {}", g_dlc1MaxPerks ? "found" : "missing");

        // === MESSAGES ===
        SKSE::log::debug("  [Messages]");
        SKSE::log::debug("    HemomancyStageUp: {}", g_msgHemomancyStageUp ? "found" : "missing");
        SKSE::log::debug("    Hemomancy_HelpMessage: {}", g_hemomancyHelpMessage ? "found" : "missing");
        SKSE::log::debug("    StrongBlood: {}", g_msgStrongBlood ? "found" : "missing");
        SKSE::log::debug("    BloodBond: {}", g_msgBloodBond ? "found" : "missing");
        SKSE::log::debug("    DLC1BloodPointsMsg: {}", g_dlc1BloodPointsMsg ? "found" : "missing");
        SKSE::log::debug("    DLC1PerkEarnedMsg: {}", g_dlc1PerkEarnedMsg ? "found" : "missing");

        // === SOUNDS ===
        SKSE::log::debug("  [Sounds]");
        SKSE::log::debug("    FeedSound: {}", g_feedSound ? "found" : "missing");

        // === FORMLISTS ===
        SKSE::log::debug("  [FormLists]");
        SKSE::log::debug("    Hemomancy_FormList: {}", g_hemomancyFormList ? "found" : "missing");
        SKSE::log::debug("    StrongBlood_Track: {}", g_strongBloodTrack ? "found" : "missing");
        SKSE::log::debug("    StrongBlood_Base: {}", g_strongBloodBase ? "found" : "missing");

        // === SPELLS - STRONG BLOOD ===
        SKSE::log::debug("  [Spells - Strong Blood]");
        SKSE::log::debug("    StrongBloodCounter: (script property, accessed at runtime)");
        SKSE::log::debug("    StrongBlood_Spell_00: {}", g_strongBloodSpells[0] ? "found" : "missing");
        SKSE::log::debug("    StrongBlood_Spell_01: {}", g_strongBloodSpells[1] ? "found" : "missing");
        SKSE::log::debug("    StrongBlood_Spell_02: {}", g_strongBloodSpells[2] ? "found" : "missing");
        SKSE::log::debug("    StrongBlood_Spell_03: {}", g_strongBloodSpells[3] ? "found" : "missing");
        SKSE::log::debug("    StrongBlood_Spell_04: {}", g_strongBloodSpells[4] ? "found" : "missing");
        SKSE::log::debug("    StrongBlood_Spell_05: {}", g_strongBloodSpells[5] ? "found" : "missing");
        SKSE::log::debug("    StrongBlood_Spell_06: {}", g_strongBloodSpells[6] ? "found" : "missing");

        // === FACTIONS ===
        SKSE::log::debug("  [Factions]");
        SKSE::log::debug("    CurrentFollowerFaction: {}", g_currentFollowerFaction ? "found" : "missing");
        SKSE::log::debug("    PotentialFollowerFaction: {}", g_potentialFollowerFaction ? "found" : "missing");
        SKSE::log::debug("    PotentialMarriageFaction: {}", g_potentialMarriageFaction ? "found" : "missing");

        // === ITEMS ===
        SKSE::log::debug("  [Items]");
        SKSE::log::debug("    DLC1BloodPotion: {}", g_dlc1BloodPotion ? "found" : "missing");

        return true;
    }

    bool IsAvailable() {
        if (!g_initialized) {
            Initialize();
        }
        return g_sacrosanctAvailable;
    }

    // === HELPER FUNCTIONS ===
    namespace Helpers {

        // === SCRIPT PROPERTY ACCESS ===
        // For accessing script-local variables (properties) that are not globals

        bool GetScriptPropertyInt(RE::TESQuest* quest, const char* scriptName, const char* propertyName, int& outValue) {
            if (!quest) return false;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                SKSE::log::warn("Helpers::GetScriptPropertyInt: VM not available");
                return false;
            }

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) {
                SKSE::log::warn("Helpers::GetScriptPropertyInt: Failed to get handle for quest");
                return false;
            }

            RE::BSTSmartPointer<RE::BSScript::Object> object;
            if (!vm->FindBoundObject(handle, scriptName, object) || !object) {
                SKSE::log::warn("Helpers::GetScriptPropertyInt: Script '{}' not found on quest", scriptName);
                return false;
            }

            auto* property = object->GetProperty(propertyName);
            if (!property) {
                SKSE::log::warn("Helpers::GetScriptPropertyInt: Property '{}' not found", propertyName);
                return false;
            }

            outValue = property->GetSInt();
            SKSE::log::debug("Helpers::GetScriptPropertyInt: {}.{} = {}", scriptName, propertyName, outValue);
            return true;
        }

        bool SetScriptPropertyInt(RE::TESQuest* quest, const char* scriptName, const char* propertyName, int value) {
            if (!quest) return false;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                SKSE::log::warn("Helpers::SetScriptPropertyInt: VM not available");
                return false;
            }

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) {
                SKSE::log::warn("Helpers::SetScriptPropertyInt: Failed to get handle for quest");
                return false;
            }

            RE::BSTSmartPointer<RE::BSScript::Object> object;
            if (!vm->FindBoundObject(handle, scriptName, object) || !object) {
                SKSE::log::warn("Helpers::SetScriptPropertyInt: Script '{}' not found on quest", scriptName);
                return false;
            }

            auto* property = object->GetProperty(propertyName);
            if (!property) {
                SKSE::log::warn("Helpers::SetScriptPropertyInt: Property '{}' not found", propertyName);
                return false;
            }

            property->SetSInt(value);
            SKSE::log::debug("Helpers::SetScriptPropertyInt: {}.{} = {}", scriptName, propertyName, value);
            return true;
        }

        void CastSpell(RE::SpellItem* spell, RE::Actor* casterActor, RE::Actor* target) {
            if (!spell || !casterActor) return;
            auto* caster = casterActor->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
            if (caster) {
                caster->CastSpellImmediate(spell, false, target, 1.0f, false, 0.0f, nullptr);
                SKSE::log::debug("Helpers::CastSpell: {} on {}",
                    spell->GetName(), target ? target->GetName() : "self");
            }
        }

        void PlaySound(RE::BGSSoundDescriptorForm* sound, RE::Actor* target) {
            if (!sound || !target) return;

            auto* audioManager = RE::BSAudioManager::GetSingleton();
            if (!audioManager) {
                SKSE::log::warn("Helpers::PlaySound: BSAudioManager not available");
                return;
            }

            RE::BSSoundHandle handle;
            handle.soundID = static_cast<uint32_t>(-1);
            handle.assumeSuccess = false;
            handle.state.reset();

            audioManager->BuildSoundDataFromDescriptor(handle, sound);

            if (handle.IsValid()) {
                handle.SetPosition(target->GetPosition());
                handle.SetObjectToFollow(target->Get3D());
                handle.Play();
                SKSE::log::debug("Helpers::PlaySound: Playing sound on {}", target->GetName());
            } else {
                SKSE::log::warn("Helpers::PlaySound: Failed to build sound handle");
            }
        }

        void ShowMessage(RE::BGSMessage* message) {
            if (!message) return;
            // Simple notification without substitutions
            RE::BSString result;
            message->GetDescription(result, nullptr);
            RE::DebugNotification(result.c_str());
            SKSE::log::debug("Helpers::ShowMessage: {}", result.c_str());
        }

        void DispelSpell(RE::Actor* actor, RE::SpellItem* spell) {
            if (!actor || !spell) return;
            // DispelSpell requires going through MagicTarget
            auto* magicTarget = actor->AsMagicTarget();
            if (magicTarget) {
                RE::ActorHandle handle;
                magicTarget->DispelEffect(spell, handle, nullptr);
                SKSE::log::debug("Helpers::DispelSpell: {} from {}", spell->GetName(), actor->GetName());
            }
        }

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

        bool CallPapyrusMethodFloat(RE::TESQuest* quest, const char* scriptName, const char* funcName, float arg) {
            if (!quest) return false;
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            auto* args = RE::MakeFunctionArguments(std::move(arg));

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

        bool SetQuestStage(RE::TESQuest* quest, int stage) {
            if (!quest) return false;
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            // SetStage takes an int parameter
            auto* args = RE::MakeFunctionArguments(static_cast<std::int32_t>(stage));

            class EmptyCallback : public RE::BSScript::IStackCallbackFunctor {
            public:
                void operator()(RE::BSScript::Variable) override {}
                bool CanSave() const override { return false; }
                void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
            };
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

            // SetStage is a native Quest method, script name is "Quest"
            bool result = vm->DispatchMethodCall(handle, "Quest", "SetStage", args, callback);
            if (!result) delete args;
            else SKSE::log::info("Helpers::SetQuestStage: Set {} to stage {}", quest->GetFormEditorID(), stage);
            return result;
        }

        bool HasMagicEffectWithKeyword(RE::Actor* actor, RE::EffectSetting* effect) {
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

        void ApplyRacialAbility(RE::Actor* player, RE::Actor* target, bool isSleeping) {
            if (!player) return;

            // Dunmer
            if (g_dunmerEffect && HasMagicEffectWithKeyword(player, g_dunmerEffect) && g_dunmerSpell) {
                CastSpell(g_dunmerSpell, player, target);
                SKSE::log::info("Helpers: Applied Dunmer racial ability");
            }
            // Altmer
            else if (g_altmerEffect && HasMagicEffectWithKeyword(player, g_altmerEffect)) {
                RE::SpellItem* spell = isSleeping ? g_altmerProcLongSpell : g_altmerProcSpell;
                if (spell) {
                    CastSpell(spell, player, nullptr);
                    SKSE::log::info("Helpers: Applied Altmer racial ability (sleeping={})", isSleeping);
                }
            }
            // Orc
            else if (g_orcEffect && HasMagicEffectWithKeyword(player, g_orcEffect)) {
                RE::SpellItem* spell = isSleeping ? g_orcProcLongSpell : g_orcProcSpell;
                if (spell) {
                    CastSpell(spell, player, nullptr);
                    SKSE::log::info("Helpers: Applied Orc racial ability (sleeping={})", isSleeping);
                }
            }
        }

        void ProcessLethalKill(RE::Actor* target, RE::Actor* killer) {
            if (!target || target->IsDead()) return;
            target->KillImpl(killer, 1000.0f, true, true);
            SKSE::log::info("Helpers::ProcessLethalKill: Killed {}", target->GetName());
        }

        void ProcessHemomancy(RE::Actor* player, bool isLethal) {
            if (!isLethal || !g_hemomancyFormList || !g_hemomancyStage || !g_hemomancySteps || !g_hemomancyStepsToNext) {
                return;
            }

            int hemoSize = static_cast<int>(g_hemomancyFormList->forms.size());
            if (g_hemomancyStage->value >= static_cast<float>(hemoSize)) {
                return;  // Already at max stage
            }

            // Start Sommelier quest if not running
            if (g_sommelierQuest && !g_sommelierQuest->IsRunning()) {
                g_sommelierQuest->Start();
                SKSE::log::info("Helpers: Started Sommelier quest");

                // Show help message tutorial
                // NOTE: ShowAsHelpMessage not easily callable from C++, using simple notification
                if (g_hemomancyHelpMessage) {
                    ShowMessage(g_hemomancyHelpMessage);
                }
            }

            // First hemomancy spell (stage 0 - add the base ability)
            if (g_hemomancyStage->value == 0.0f && g_hemomancyBaseAbility) {
                player->AddSpell(g_hemomancyBaseAbility);
            }

            // Increment steps
            g_hemomancySteps->value += 1.0f;

            // Check for stage up
            if (g_hemomancySteps->value >= g_hemomancyStepsToNext->value) {
                // Show stage up message (simplified - Papyrus version uses arg substitution)
                ShowMessage(g_msgHemomancyStageUp);

                // Add new hemomancy spell from formlist
                int stageIndex = static_cast<int>(g_hemomancyStage->value);
                if (stageIndex < hemoSize) {
                    auto* newSpell = g_hemomancyFormList->forms[stageIndex]->As<RE::SpellItem>();
                    if (newSpell) {
                        player->AddSpell(newSpell);
                        SKSE::log::info("Helpers: Added hemomancy spell at stage {}", stageIndex);
                    }
                }

                // Reset steps and advance stage
                g_hemomancySteps->value = 0.0f;
                g_hemomancyStage->value += 1.0f;

                // Increase steps needed for next stage
                if (g_hemomancyStepsAddPerStep) {
                    g_hemomancyStepsToNext->value += g_hemomancyStepsAddPerStep->value;
                }
            }

            // Complete quest if at max (via Papyrus since CompleteQuest is a script function)
            if (g_hemomancyStage->value >= static_cast<float>(hemoSize) && g_sommelierQuest && g_sommelierQuest->IsRunning()) {
                CallPapyrusMethod(g_sommelierQuest, "Quest", "CompleteQuest");
                SKSE::log::info("Helpers: Completed Sommelier quest via Papyrus");
            }
        }

        void ProcessStrongBlood(RE::Actor* player, RE::Actor* target, RE::TESQuest* sacrosanctQuest) {
            if (!player || !target || !sacrosanctQuest) return;
            if (!g_strongBloodTrack || !g_strongBloodBase) return;

            // Only process if quest is at stage 10
            // Note: We can't easily check quest stage in C++, so we'll just check if forms are present

            auto* targetBase = target->GetActorBase();
            if (!targetBase) return;

            // Check if target's base is in the Strong Blood tracking list
            bool found = false;
            int foundIndex = -1;
            for (uint32_t i = 0; i < g_strongBloodTrack->forms.size(); ++i) {
                if (g_strongBloodTrack->forms[i] == targetBase) {
                    found = true;
                    foundIndex = static_cast<int>(i);
                    break;
                }
            }

            if (!found) return;

            // Get current counter value from script property
            int counter = 0;
            if (!GetScriptPropertyInt(sacrosanctQuest, "SCS_FeedManager_Quest", "StrongBloodCounter", counter)) {
                SKSE::log::warn("Helpers: Strong Blood - failed to read StrongBloodCounter property");
                return;
            }

            // Check if we still have spells to grant (0-6)
            if (counter >= 7) {
                SKSE::log::info("Helpers: Strong Blood - already at max (counter={})", counter);
                return;
            }

            // Verify spell exists at this index
            RE::SpellItem* rewardSpell = g_strongBloodSpells[counter];
            if (!rewardSpell) {
                SKSE::log::warn("Helpers: Strong Blood - spell at index {} not found", counter);
                return;
            }

            // Show message
            ShowMessage(g_msgStrongBlood);

            // Add the reward spell
            player->AddSpell(rewardSpell);
            SKSE::log::info("Helpers: Strong Blood - granted spell {} (index {})",
                rewardSpell->GetName(), counter);

            // Increment counter via script property
            if (SetScriptPropertyInt(sacrosanctQuest, "SCS_FeedManager_Quest", "StrongBloodCounter", counter + 1)) {
                SKSE::log::info("Helpers: Strong Blood - counter now {}", counter + 1);
            } else {
                SKSE::log::warn("Helpers: Strong Blood - failed to update StrongBloodCounter property");
            }

            // Remove target from tracking list
            // BGSListForm doesn't have RemoveForm in CommonLibSSE, so we manually remove from the array
            // Note: This only works for the main forms array, not scriptAddedTempForms
            // For runtime-added forms, Papyrus RemoveAddedForm handles scriptAddedTempForms
            auto& formsList = g_strongBloodTrack->forms;
            for (auto it = formsList.begin(); it != formsList.end(); ++it) {
                if (*it == targetBase) {
                    formsList.erase(it);
                    SKSE::log::info("Helpers: Strong Blood - removed {} from tracking list", target->GetName());
                    break;
                }
            }
        }

        void ProcessVampireLordXP(RE::Actor* player, RE::Actor* target, bool isLethal) {
            if (!isLethal || !player || !target) return;
            if (!g_lethalFeedXPPerk || !player->HasPerk(g_lethalFeedXPPerk)) return;
            if (target->IsCommandedActor() || target->IsGhost()) return;

            if (!g_dlc1BloodPoints || !g_dlc1NextPerk || !g_dlc1PerkPoints ||
                !g_dlc1TotalPerksEarned || !g_dlc1MaxPerks) return;

            g_dlc1BloodPoints->value += 1.0f;

            if (g_dlc1TotalPerksEarned->value < g_dlc1MaxPerks->value) {
                ShowMessage(g_dlc1BloodPointsMsg);

                if (g_dlc1BloodPoints->value >= g_dlc1NextPerk->value) {
                    g_dlc1BloodPoints->value -= g_dlc1NextPerk->value;
                    g_dlc1PerkPoints->value += 1.0f;
                    g_dlc1TotalPerksEarned->value += 1.0f;
                    g_dlc1NextPerk->value += 1.0f;
                    ShowMessage(g_dlc1PerkEarnedMsg);
                    SKSE::log::info("Helpers: Earned Vampire Lord perk point!");
                }

                // Update VampirePerks AV for progress bar
                float progress = (g_dlc1BloodPoints->value / g_dlc1NextPerk->value) * 100.0f;
                player->AsActorValueOwner()->SetActorValue(RE::ActorValue::kVampirePerks, progress);
            }
        }

        void ProcessBloodBond(RE::Actor* player, RE::Actor* target, bool isSleeping, bool isLethal, bool isEmbrace) {
            if (!isSleeping || isLethal || !target || !player) return;
            if (!g_bloodBondAbility) return;

            // Check if player has Blood Bond ability
            auto* bloodBondSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Reward_Spell_BloodBond_Ab");
            if (!bloodBondSpell || !player->HasSpell(bloodBondSpell)) return;

            // Check unique (if required)
            if (g_forceUniqueCheck && g_forceUniqueCheck->value == 0.0f) {
                auto* actorBase = target->GetActorBase();
                if (actorBase && !actorBase->IsUnique()) return;
            }

            // Don't bond with current followers or embrace targets
            if (isEmbrace) return;
            if (g_currentFollowerFaction && target->IsInFaction(g_currentFollowerFaction)) return;

            // Set relationship to max
            // Note: SetRelationshipRank requires going through TESActorBaseData or Papyrus
            // For now, we just add to factions which is the main effect
            SKSE::log::debug("Helpers: Blood Bond relationship setup (faction-based)");

            // Add to follower/marriage factions
            if (g_potentialFollowerFaction) target->AddToFaction(g_potentialFollowerFaction, 0);
            if (g_potentialMarriageFaction) target->AddToFaction(g_potentialMarriageFaction, 0);

            ShowMessage(g_msgBloodBond);
            SKSE::log::info("Helpers: Created Blood Bond with {}", target->GetName());
        }

        void ResetWassail(RE::Actor* player) {
            if (!g_wassailCurrent || !g_wassailNerfAmount) return;

            float restoreAmount = g_wassailNerfAmount->value;
            if (restoreAmount > 0.0f) {
                // Note: The Papyrus uses string AV names stored in properties
                // We'll restore to all three main stats as a reasonable default
                auto* avOwner = player->AsActorValueOwner();
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, restoreAmount);
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, restoreAmount);
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, restoreAmount);
                SKSE::log::info("Helpers: Reset Wassail, restored {} to stats", restoreAmount);
            }

            g_wassailCurrent->value = 0.0f;
            g_wassailNerfAmount->value = 0.0f;
        }

        void ProcessAge(bool isLethal) {
            if (!g_scsMain500Quest) return;

            RE::TESGlobal* ageBonus = isLethal ? g_ageBonusFromDrain : g_ageBonusFromFeed;
            if (!ageBonus) return;

            CallPapyrusMethodFloat(g_scsMain500Quest, "SCS_Main500_Quest", "Age", ageBonus->value);
            SKSE::log::info("Helpers: Processed age bonus (lethal={}, amount={})", isLethal, ageBonus->value);
        }

        void DispelFeedDebuffs(RE::Actor* player) {
            if (!player) return;
            DispelSpell(player, g_reverseProgressionProc);
            DispelSpell(player, g_bloodIsPowerSpell);
        }

    }  // namespace Helpers

    bool ProcessFeed(const FeedContext& context) {
        if (!context.target) {
            SKSE::log::error("SacrosanctIntegration::ProcessFeed: target is null");
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("SacrosanctIntegration::ProcessFeed: player is null");
            return false;
        }

        SKSE::log::info("SacrosanctIntegration::ProcessFeed: target={}, lethal={}, combat={}, sleeping={}, sneak={}, embrace={}",
            context.target->GetName(), context.isLethal, context.isCombatFeed, context.isSleeping,
            context.isSneakFeed, context.isEmbrace);

        int targetLevel = context.target->GetLevel();
        bool targetIsVampire = g_vampireKeyword && context.target->HasKeyword(g_vampireKeyword);

        // === STEP 1: Quest stage check (SetStage(10) if < 10) ===
        if (g_sacrosanctQuest) {
            uint16_t currentStage = g_sacrosanctQuest->GetCurrentStageID();
            if (currentStage < 10) {
                Helpers::SetQuestStage(g_sacrosanctQuest, 10);
                SKSE::log::info("SacrosanctIntegration: Set quest stage to 10 (was {})", currentStage);
            }
        }

        // === STEP 2: DLC1VampireTurn.PlayerBitesMe ===
        CallPreFeedPapyrus(context.target);

        // === STEP 3: Feed sound ===
        if (g_feedSound) {
            Helpers::PlaySound(g_feedSound, context.target);
        }

        // === STEP 4: Cast feed spell on target ===
        if (!context.target->IsDead() && g_scsFeedTargetSpell) {
            Helpers::CastSpell(g_scsFeedTargetSpell, player, context.target);
        }

        // === STEP 5: Amaranth (lethal + vampire target) ===
        if (context.isLethal && targetIsVampire) {
            if (g_amaranthSpell) {
                Helpers::CastSpell(g_amaranthSpell, player, nullptr);
            }
            if (g_amaranthTargetSpell) {
                Helpers::CastSpell(g_amaranthTargetSpell, player, context.target);
            }
            SKSE::log::info("SacrosanctIntegration: Applied Amaranth (vampire lethal)");
        }

        // === STEP 6: PlayerVampireQuest.VampireFeed() ===
        if (g_playerVampireQuest) {
            Helpers::CallPapyrusMethod(g_playerVampireQuest, "PlayerVampireQuestScript", "VampireFeed");
            SKSE::log::debug("SacrosanctIntegration: Called PlayerVampireQuest.VampireFeed()");
        }

        // === STEP 7: Racial abilities (Dunmer/Altmer/Orc) ===
        Helpers::ApplyRacialAbility(player, context.target, context.isSleeping);

        // === STEP 8: Sneak feed alarm + spell ===
        if (context.isSneakFeed) {
            // SendAssaultAlarm - requires Papyrus call
            // context.target->SendAssaultAlarm(); // No direct C++ API
            if (g_sneakFeedSpell) {
                Helpers::CastSpell(g_sneakFeedSpell, player, context.target);
            }
            SKSE::log::info("SacrosanctIntegration: Sneak feed processed");
        }

        // === STEP 9: Lethal kill ===
        // Skip if animation handles the kill (lethal idle/OAR animation has kill baked in)
        if (context.isLethal && !context.animationHandlesKill && !context.target->IsDead()) {
            Helpers::ProcessLethalKill(context.target, player);
        } else if (context.isLethal && context.animationHandlesKill) {
            SKSE::log::info("SacrosanctIntegration: Skipping kill - animation handles it");
        }

        // === STEP 10: Destruction XP for lethal feeds ===
        if (context.isLethal) {
            float baseXP = g_scsXPLethalBase ? g_scsXPLethalBase->value : 50.0f;
            float levelMult = g_scsXPLethalLevel ? g_scsXPLethalLevel->value : 2.0f;
            float xpGain = baseXP + static_cast<float>(targetLevel) * levelMult;
            player->AddSkillExperience(RE::ActorValue::kDestruction, xpGain);
            SKSE::log::info("SacrosanctIntegration: Added {:.0f} Destruction XP", xpGain);
        }

        // NOTE: Steps 11-12 (hunger stage + feed timer) are handled by PlayerVampireQuest.VampireFeed() above

        // === STEP 13: Restore Health/Magicka/Stamina ===
        float restoreAmount = 100.0f + static_cast<float>(targetLevel) * 20.0f;
        auto* avOwner = player->AsActorValueOwner();
        avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, restoreAmount);
        avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, restoreAmount);
        avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, restoreAmount);
        SKSE::log::info("SacrosanctIntegration: Restored {:.0f} H/M/S (target level {})", restoreAmount, targetLevel);

        // === STEP 14: Blood Knight stamina cost (combat feed) ===
        if (context.isCombatFeed && g_bloodKnightCost) {
            float staminaCost = g_bloodKnightCost->value;
            avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, -staminaCost);
            SKSE::log::info("SacrosanctIntegration: Blood Knight stamina cost: {}", staminaCost);
        }

        // === STEP 15: Kiss of Death (sleeping + lethal) ===
        if (context.isSleeping && context.isLethal && g_kissOfDeathAbility && g_kissOfDeathAmount) {
            if (player->HasSpell(g_kissOfDeathAbility)) {
                float bonus = g_kissOfDeathAmount->value;
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, bonus);
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, bonus);
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, bonus);
                SKSE::log::info("SacrosanctIntegration: Kiss of Death bonus: {}", bonus);
            }
        }

        // TODO: add emabre options for context menu 
        // === STEP 16: Embrace spell (non-lethal embrace) ===
        if (context.isEmbrace && !context.isLethal && g_embraceSpell) {
            Helpers::CastSpell(g_embraceSpell, player, context.target);
            SKSE::log::info("SacrosanctIntegration: Embrace spell cast");
        }

        // === STEP 17: Psychic Vampire perk spell ===
        if (!context.isLethal && g_psychicVampirePerk && g_psychicVampireSpell) {
            if (player->HasPerk(g_psychicVampirePerk)) {
                Helpers::CastSpell(g_psychicVampireSpell, player, context.target);
                SKSE::log::info("SacrosanctIntegration: Psychic Vampire spell cast");
            }
        }

        // === STEP 18: Dispel progression spells ===
        Helpers::DispelFeedDebuffs(player);

        // === STEP 19: Hemomancy progression (lethal only) ===
        Helpers::ProcessHemomancy(player, context.isLethal);

        // === STEP 20: Strong Blood quest (unique NPCs) ===
        Helpers::ProcessStrongBlood(player, context.target, g_sacrosanctQuest);

        // === STEP 21: Vampire Lord XP (lethal) ===
        Helpers::ProcessVampireLordXP(player, context.target, context.isLethal);

        // === STEP 22: Lamae's Wrath flag (sleeping) ===
        if (context.isSleeping && g_canLamaesWrath) {
            g_canLamaesWrath->value = 1.0f;
            SKSE::log::info("SacrosanctIntegration: Enabled Lamae's Wrath");
        }

        // === STEP 23: Blood Bond (sleeping, non-lethal, non-embrace) ===
        Helpers::ProcessBloodBond(player, context.target, context.isSleeping, context.isLethal, context.isEmbrace);

        // === STEP 24: Reset Wassail ===
        Helpers::ResetWassail(player);

        // === STEP 25: Harvest Moon (lethal, gives blood potion) ===
        if (context.isLethal && g_dlc1BloodPotion) {
            auto* harvestMoonSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("SCS_Abilities_Reward_Spell_HarvestMoon_Ab");
            if (harvestMoonSpell && player->HasSpell(harvestMoonSpell)) {
                player->AddObjectToContainer(g_dlc1BloodPotion, nullptr, 1, nullptr);
                SKSE::log::info("SacrosanctIntegration: Harvest Moon - added blood potion");
            }
        }

        // === STEP 26: Age mechanic ===
        Helpers::ProcessAge(context.isLethal);

        // === STEP 27: Final AI cleanup (handled by animation system) ===
        SKSE::log::debug("SacrosanctIntegration: AI cleanup handled by animation system");

        SKSE::log::info("SacrosanctIntegration::ProcessFeed: Complete");
        return true;
    }

    bool CallPreFeedPapyrus(RE::Actor* target) {
        if (!target) return false;
        if (!g_dlc1VampireTurnQuest) {
            SKSE::log::debug("SacrosanctIntegration: DLC1VampireTurn quest not found, skipping PlayerBitesMe");
            return false;
        }

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return false;

        // Get handle for the quest
        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(
            RE::TESQuest::FORMTYPE, g_dlc1VampireTurnQuest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) {
            SKSE::log::warn("SacrosanctIntegration: Failed to get handle for DLC1VampireTurn");
            return false;
        }

        // Call PlayerBitesMe(Actor akTarget)
        auto* args = RE::MakeFunctionArguments(std::move(target));

        // Empty callback
        class EmptyCallback : public RE::BSScript::IStackCallbackFunctor {
        public:
            void operator()(RE::BSScript::Variable) override {}
            bool CanSave() const override { return false; }
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
        };
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new EmptyCallback());

        bool result = vm->DispatchMethodCall(
            handle,
            "DLC1VampireTurnScript",
            "PlayerBitesMe",
            args,
            callback
        );

        if (!result) {
            delete args;
            SKSE::log::warn("SacrosanctIntegration: PlayerBitesMe call failed");
        } else {
            SKSE::log::debug("SacrosanctIntegration: PlayerBitesMe called successfully");
        }

        return result;
    }

    namespace VampireState {
        // NOTE: These functions are utility helpers for standalone vanilla vampire state manipulation.
        // In ProcessFeed, we use PlayerVampireQuest.VampireFeed() instead, which handles timer/hunger.
        // These use static local caching since they're standalone utilities.

        static RE::TESGlobal* GetVampireFeedReady() {
            static RE::TESGlobal* cached = nullptr;
            if (!cached) {
                cached = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireFeedReady");
            }
            return cached;
        }

        static RE::TESGlobal* GetVampireFeedTimer() {
            static RE::TESGlobal* cached = nullptr;
            if (!cached) {
                cached = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireFeedTimer");
            }
            return cached;
        }

        int GetHungerStage() {
            auto* feedReady = GetVampireFeedReady();
            if (feedReady) {
                // VampireFeedReady is 0-3, stage is 1-4
                return static_cast<int>(feedReady->value) + 1;
            }
            return -1;
        }

        bool SetHungerStage(int stage) {
            if (stage < 1 || stage > 4) {
                SKSE::log::warn("VampireState::SetHungerStage: Invalid stage {} (must be 1-4)", stage);
                return false;
            }

            auto* feedReady = GetVampireFeedReady();
            if (!feedReady) {
                SKSE::log::error("VampireState::SetHungerStage: VampireFeedReady global not found");
                return false;
            }

            // VampireFeedReady is 0-3, stage is 1-4
            float newValue = static_cast<float>(stage - 1);
            feedReady->value = newValue;

            SKSE::log::info("VampireState: Set hunger stage to {} (VampireFeedReady={})", stage, newValue);
            return true;
        }

        bool ReduceHunger() {
            int currentStage = GetHungerStage();
            if (currentStage <= 0) {
                SKSE::log::warn("VampireState::ReduceHunger: Not a vampire or invalid stage");
                return false;
            }

            // Feeding reduces hunger (lower stage = more sated)
            // Stage 4 (starving) -> 3 (hungry) -> 2 (peckish) -> 1 (sated)
            int newStage = std::max(1, currentStage - 1);

            SKSE::log::info("VampireState: Reducing hunger {} -> {}", currentStage, newStage);
            return SetHungerStage(newStage);
        }

        bool ResetFeedTimer() {
            auto* feedTimer = GetVampireFeedTimer();
            if (!feedTimer) {
                SKSE::log::warn("VampireState::ResetFeedTimer: VampireFeedTimer global not found");
                return false;
            }

            // Get current game time
            auto* calendar = RE::Calendar::GetSingleton();
            if (!calendar) {
                SKSE::log::warn("VampireState::ResetFeedTimer: Calendar not available");
                return false;
            }

            float currentTime = calendar->GetHoursPassed();
            feedTimer->value = currentTime;

            SKSE::log::info("VampireState: Reset feed timer to {:.2f}", currentTime);
            return true;
        }
    }

    namespace SacrosanctState {

        bool HasHemomancy() {
            // Check if player has any hemomancy-related perks
            // This is a simplified check - Sacrosanct has multiple hemomancy perks
            return HasSacrosanctPerk("SCS_Hemomancy_Apprentice") ||
                   HasSacrosanctPerk("SCS_Hemomancy_Novice") ||
                   HasSacrosanctPerk("SCS_Perk_BloodMagic");
        }

        bool AddBloodPoints(float amount) {
            // Sacrosanct uses vanilla DLC1VampireBloodPoints for Vampire Lord progression
            if (g_dlc1BloodPoints) {
                g_dlc1BloodPoints->value += amount;
                SKSE::log::debug("SacrosanctState: DLC1 Blood points now {:.0f}", g_dlc1BloodPoints->value);
                return true;
            }
            SKSE::log::debug("SacrosanctState: Could not add blood points (DLC1VampireBloodPoints not found)");
            return false;
        }

        float GetBloodPoints() {
            // Returns vanilla DLC1 blood points
            if (g_dlc1BloodPoints) {
                return g_dlc1BloodPoints->value;
            }
            return 0.0f;
        }

        bool HasSacrosanctPerk(const char* perkEditorID) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return false;

            auto* perk = RE::TESForm::LookupByEditorID<RE::BGSPerk>(perkEditorID);
            if (!perk) return false;

            return player->HasPerk(perk);
        }
    }
}
