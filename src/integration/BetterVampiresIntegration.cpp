#include "PCH.h"
#include "BetterVampiresIntegration.h"
#include "VampireIntegrationUtils.h"

#include <chrono>
#include <thread>

/*
 * =============================================================================
 * BETTER VAMPIRES VAMPIREFEED - C++ IMPLEMENTATION
 * =============================================================================
 *
 * Based on PlayerVampireQuestScript.VampireFeed() from Better Vampires 9.1.
 *
 * PAPYRUS FUNCTION SIGNATURE:
 *   VampireFeed(Actor akFeedTarget)
 *
 * The per-state FeedX() sub-routes (FeedDead/FeedSneaking/FeedSleeping/...)
 * only vary in blood points, victim marking, the drain kill and cosmetics -
 * those are folded into steps 8-11. Heavy helpers stay in Papyrus (dispatch).
 *
 * =============================================================================
 * IMPLEMENTATION STEPS
 * =============================================================================
 *
 * STEP | FEATURE                                    | STATUS
 * -----|--------------------------------------------|--------------------------
 *  1   | UsingBetterVampiresScripts = 3             | ✅ DONE (left at 3; BV never resets it in VampireFeed)
 *  2   | Menu spell housekeeping (VampireMenuSpell) | ✅ DONE
 *  3   | AddToFaction(VampirePCFamily)              | ✅ DONE
 *  4   | DisablePlayerControls + Utility.Wait       | SKIP - our animation owns controls
 *  5   | Route: MoveTo / PlayIdle vanilla anims     | SKIP - replaced by our paired anims
 *  6   | Route: SendAssaultAlarm                    | SKIP - WitnessDetection owns crime
 *  7   | Route: sneak escape minigame (20/20/60)    | SKIP - no sneak route; our gate decides feed eligibility, not BV sneak state
 *  8   | Route: blood points (+50/+75/+100/+150)    | ✅ DONE
 *  9   | Route: victim mark Variable08/05, drain    | ✅ DONE
 * 10   | Route: lethal drain kill + essential msgs  | ✅ DONE
 * 11   | Route: feed sound / BleedingSpell /        | ✅ DONE
 *      |   NeckMarksRight shader / screen blood     |
 * 12   | AmaranthGainSkills (vampire victims)       | ✅ DONE - Papyrus dispatch
 * 13   | TurnNPCIntoVampire (CreateVampire > 0)     | ✅ DONE - Papyrus dispatch; ELSE-gates 14-23
 * 14   | Red screen ISM crossfade                   | ✅ DONE - crossfade + timed removal
 * 15   | Extract-blood perk sync                    | ✅ DONE
 * 16   | Victim 25% health drain + VictimDamage2    | ✅ DONE
 * 17   | Player restore + Engorge (rank >= 60000)   | ✅ DONE
 * 18   | Necks Bitten stat + discovery by location  | ✅ DONE
 * 19   | Rank progression (3 modes)                 | ✅ DONE - Papyrus dispatch
 * 20   | Rank status notifications                  | ✅ DONE - via async QueryStat
 * 21   | SpecialFeedingBonus (famous NPCs)          | ✅ DONE - Papyrus dispatch
 * 22   | VampireFeedReady = 0                       | ✅ DONE
 * 23   | Satiation stages (3 modes) + LastTimeFed   | ✅ DONE - Papyrus dispatch
 * 24   | Dead target Variable08 = 9                 | ✅ DONE
 * 25   | EnablePlayerControls / SetPlayerAIDriven   | SKIP - our teardown handles
 * 26   | Sneak victim 15s fear reset                | SKIP - only fires after the sneak minigame, which we never enter
 * 27   | Skill point chance (2/100)                 | ✅ DONE
 * 28   | Reset BottledBlood/ExtractingBlood globals | ✅ DONE
 * 29   | Re-register UpdateGameTime (hunger tick)   | ✅ DONE - Papyrus dispatch
 *
 * =============================================================================
 * KNOWN LIMITATIONS
 * =============================================================================
 *
 * - Hunger-stage progression runs on BV's own OnUpdateGameTime tick; we re-arm it
 *   (RegisterForUpdateGameTime) but do not reimplement it. BV reads its own script
 *   properties there, so a save with unfilled (None) BV properties has broken hunger
 *   regardless of this integration.
 * - Feed sound is a vanilla SOUN whose EditorID isn't cached; resolved by FormID
 *   (descriptor MAGVampireTransform01SD = 0x000FF9E8).
 * - StartVampireFeed and per-route DLC1VampireTurn.PlayerBitesMe are skipped (no-ops
 *   for an already-turned vampire).
 * - Requires BV 9.1+ script shape (TurnedNPCRefresh present); 8.9 or older disables
 *   the deep path and falls back to Papyrus.
 *
 * =============================================================================
 * FORM DEPENDENCIES (cached in Initialize())
 * =============================================================================
 *
 * Quests:
 *   - PlayerVampireQuest
 *
 * Globals (BV):
 *   - State/flags: UsingBetterVampiresScripts, VampireFeedReady, VampireBloodPoints,
 *     EnableVampireBloodPoints, VampireDynamicStages, VampireFeedOffDead, CreateVampire,
 *     VampireExtractingBlood, VampireBottledBlood, TargetAlreadyDeadGlobal
 *   - Rank/stage: VampireRank, VampireRankProgression, VampireEngorge, VampireEngorgeAmount
 *   - Hunger tick gates: BVCalculateFeedTimer, VampireUpdateGameTime, VampireLastTimeFed
 *   - Notoriety: VampireNecksBittenDiscovered
 *   - Toggles: VampireMenuSpell, VampireExtractBlood, VampireNeckMarks, VampireNoRedScreen,
 *     VampireStatusMessages, BVSpecialVictimFeeding
 *   - Skill points: BVMCMSkillPointsTotal, BVMCMSkillPointsAvailable, BVMCMGiveAllSkillPointsGlobal
 *
 * Globals (vanilla):
 *   - GameDaysPassed
 *
 * Factions:   VampirePCFamily
 * Keywords:   Vampire
 * Spells:     BetterVampiresMenuOptionsSpell, VampireVictimDamage2, BleedingSpell
 * Perks:      VampireExtractBloodPotions
 * Sounds:     MAGVampireTransform01 (SOUN) / MAGVampireTransform01SD (descriptor)
 * Shaders:    NeckMarksRight
 * ImageSpace: VampireTransformDecreaseISMD
 * FormLists:  BVPowerfulFeedingVictims
 * Locations:  9 cities (+2 notoriety), 9 towns (+1), else +0.5
 *
 * =============================================================================
 * PAPYRUS FUNCTIONS DISPATCHED (on PlayerVampireQuestScript)
 * =============================================================================
 *
 *   - TurnNPCIntoVampire, AmaranthGainSkills, SpecialFeedingBonus
 *   - Rank: NormalRankProgression, EasierRankProgression, DaysAsVampireProgression
 *   - Satiation: NormalStagesSatiation, DynamicStagesSatiation, TwoStagesSatiation
 *   - Hunger tick: RegisterForUpdateGameTime
 *
 * =============================================================================
 * CUSTOM PERK ABILITIES - NOT REIMPLEMENTED
 * =============================================================================
 *
 * BV fires its abilities from perk-fragment entry points (PRKF_* scripts, all
 * Fragment_N(ObjectReference akTargetRef, Actor akActor) - separate points for
 * standing / sneaking / in-bed / dead-target / custom-race). Each fragment calls
 * StartVampireFeed + one quest-script ability. We replace those entry points with
 * our own eligibility gate + prompt, so NO PRKF_* fragment runs and only the ability
 * we reimplement (VampireFeed) is covered.
 *
 * Quest-script abilities (PlayerVampireQuestScript):
 *   - VampireFeed(Actor)     -> DONE   (ProcessFeed reimplements it)
 *   - VampireBite(Actor)     -> COVERED (our composite non-lethal feed is a partial bite; separate impl likely unneeded)
 *   - VampireEnthrall(Actor) -> NOT COVERED (binds an NPC as an enthralled thrall)
 *
 * Feed variants NOT reimplemented (they only reach BV via the skipped PRKF_* fragments):
 *   - Extract blood -> potion (VampireExtractingBlood=10000 + DLC1BloodPotion/DLC1BloodPotion2)
 *   - Enthrall
 *
 * =============================================================================
 */

namespace BetterVampiresIntegration {

    namespace {
        constexpr const char* kScriptName = "PlayerVampireQuestScript";

        std::atomic<bool> g_initialized{false};
        std::atomic<bool> g_available{false};
        std::atomic<bool> g_shapeVerified{false};
        std::atomic<const char*> g_versionInfo{"not detected"};

        // Globals
        RE::TESGlobal* g_usingBVScripts = nullptr;
        RE::TESGlobal* g_menuSpellToggle = nullptr;
        RE::TESGlobal* g_bottledBlood = nullptr;
        RE::TESGlobal* g_extractingBlood = nullptr;
        RE::TESGlobal* g_feedReady = nullptr;
        RE::TESGlobal* g_bloodPoints = nullptr;
        RE::TESGlobal* g_enableBloodPoints = nullptr;
        RE::TESGlobal* g_dynamicStages = nullptr;
        RE::TESGlobal* g_feedOffDead = nullptr;
        RE::TESGlobal* g_createVampire = nullptr;
        RE::TESGlobal* g_vampireRank = nullptr;
        RE::TESGlobal* g_engorge = nullptr;
        RE::TESGlobal* g_engorgeAmount = nullptr;
        RE::TESGlobal* g_extractBloodToggle = nullptr;
        RE::TESGlobal* g_necksBittenDiscovered = nullptr;
        RE::TESGlobal* g_rankProgression = nullptr;
        RE::TESGlobal* g_statusMessages = nullptr;
        RE::TESGlobal* g_specialVictimFeeding = nullptr;
        RE::TESGlobal* g_lastTimeFed = nullptr;
        RE::TESGlobal* g_gameDaysPassed = nullptr;
        RE::TESGlobal* g_skillPointsTotal = nullptr;
        RE::TESGlobal* g_skillPointsAvailable = nullptr;
        RE::TESGlobal* g_giveAllSkillPoints = nullptr;
        RE::TESGlobal* g_neckMarksToggle = nullptr;
        RE::TESGlobal* g_targetAlreadyDeadGlobal = nullptr;
        RE::TESGlobal* g_noRedScreen = nullptr;
        RE::TESGlobal* g_calcFeedTimer = nullptr;    // BVCalculateFeedTimer - gate for FeedTimer updates
        RE::TESGlobal* g_updateGameTimeGate = nullptr; // VampireUpdateGameTime - blocks stage updates when != 0

        // Quests
        RE::TESQuest* g_playerVampireQuest = nullptr;

        // Factions
        RE::TESFaction* g_vampirePCFamily = nullptr;

        // Keywords
        RE::BGSKeyword* g_vampireKeyword = nullptr;

        // Spells
        RE::SpellItem* g_menuOptionsSpell = nullptr;
        RE::SpellItem* g_victimDamageSpell = nullptr;
        RE::SpellItem* g_bleedingSpell = nullptr;

        // Perks
        RE::BGSPerk* g_extractBloodPerk = nullptr;

        // Sounds (SOUN record; PlaySound needs its descriptor)
        RE::TESSound* g_feedSoundRecord = nullptr;
        RE::BGSSoundDescriptorForm* g_feedSound = nullptr;

        // Effect shaders
        RE::TESEffectShader* g_neckMarksShader = nullptr;

        // Image space modifiers
        RE::TESImageSpaceModifier* g_redScreenISM = nullptr;

        // FormLists
        RE::BGSListForm* g_powerfulVictims = nullptr;

        // Necks-bitten discovery weights: +2 in a city, +1 in a town, +0.5 elsewhere
        constexpr const char* kCityLocationIDs[] = {
            "DawnstarLocation", "MarkarthLocation", "MorthalLocation", "RiftenLocation", "SolitudeLocation",
            "WhiterunLocation", "WindhelmLocation", "WinterholdLocation", "FalkreathLocation"
        };
        constexpr const char* kTownLocationIDs[] = {
            "DragonBridgeLocation", "HelgenLocation", "IvarsteadLocation", "KarthwastenLocation", "RiverwoodLocation",
            "RoriksteadLocation", "ShorsStoneLocation", "DLC2RavenRockLocation", "DLC2SkaalVillageLocation"
        };
        RE::BGSLocation* g_cityLocations[std::size(kCityLocationIDs)]{};
        RE::BGSLocation* g_townLocations[std::size(kTownLocationIDs)]{};

        using VampireIntegrationUtils::PlaySound;
        using VampireIntegrationUtils::CastSpell;
        using VampireIntegrationUtils::CallPapyrusMethod;

        // Fire-and-forget dispatch of a PlayerVampireQuestScript method with an Actor arg
        bool CallBVMethod(const char* funcName, RE::Actor* actor) {
            if (!g_playerVampireQuest || !actor) return false;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, g_playerVampireQuest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new VampireIntegrationUtils::EmptyCallback());
            return vm->DispatchMethodCall(handle, kScriptName, funcName,
                RE::MakeFunctionArguments(std::move(actor)), callback);
        }

        // Float-arg variant (RegisterForUpdateGameTime)
        bool CallBVMethod(const char* funcName, float value) {
            if (!g_playerVampireQuest) return false;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, g_playerVampireQuest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new VampireIntegrationUtils::EmptyCallback());
            return vm->DispatchMethodCall(handle, kScriptName, funcName,
                RE::MakeFunctionArguments(std::move(value)), callback);
        }

        // Not cosmetic here: BV's rank progression reads this stat via QueryStat
        void IncrementNecksBittenStat() {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return;

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new VampireIntegrationUtils::EmptyCallback());
            // Game.IncrementStat(string, int aiModAmount=1) is native - dispatch must pass BOTH args
            // (no Papyrus default-filling), or the call silently no-ops and the stat never moves.
            RE::BSFixedString stat("Necks Bitten");
            std::int32_t amount = 1;
            vm->DispatchStaticCall("Game", "IncrementStat",
                RE::MakeFunctionArguments(std::move(stat), std::move(amount)), callback);
        }

        void TriggerScreenBlood(int count) {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return;

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new VampireIntegrationUtils::EmptyCallback());
            std::int32_t c = count;
            bool ok = vm->DispatchStaticCall("Game", "TriggerScreenBlood", RE::MakeFunctionArguments(std::move(c)), callback);
            SKSE::log::debug("BetterVampiresIntegration: TriggerScreenBlood({}) dispatch {}", count, ok ? "ok" : "FAILED");
        }

        // BV: ApplyCrossFade(2.0), wait 2s, RemoveCrossFade - the wait runs off-thread and
        // the removal is queued back onto the game thread
        void ApplyRedScreenCrossFade() {
            if (!g_redScreenISM) return;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESImageSpaceModifier::FORMTYPE, g_redScreenISM);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return;

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new VampireIntegrationUtils::EmptyCallback());
            float fadeDuration = 2.0f;
            bool ok = vm->DispatchMethodCall(handle, "ImageSpaceModifier", "ApplyCrossFade",
                RE::MakeFunctionArguments(std::move(fadeDuration)), callback);
            SKSE::log::debug("BetterVampiresIntegration: red screen ApplyCrossFade dispatch {}", ok ? "ok" : "FAILED");
            if (!ok) return;

            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                SKSE::GetTaskInterface()->AddTask([]() {
                    auto* taskVm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                    if (!taskVm) return;
                    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb(new VampireIntegrationUtils::EmptyCallback());
                    float removeDuration = 1.0f;
                    taskVm->DispatchStaticCall("ImageSpaceModifier", "RemoveCrossFade",
                        RE::MakeFunctionArguments(std::move(removeDuration)), cb);
                });
            }).detach();
        }

        // Receives QueryStat("Necks Bitten") and shows the rank-tier feed message
        class RankFeedNotifyCallback : public RE::BSScript::IStackCallbackFunctor {
        public:
            void operator()(RE::BSScript::Variable a_result) override {
                if (!a_result.IsInt()) return;

                const float rank = g_vampireRank ? g_vampireRank->value : 0.0f;
                const char* text = nullptr;
                if (rank == 10000.0f) text = " feedings begin to fill me with power.";
                else if (rank == 20000.0f) text = " feedings fill me with a growing sense of power.";
                else if (rank == 30000.0f) text = " feedings further enhance my vampiric powers.";
                else if (rank == 40000.0f) text = " feedings fuel my formidable vampiric powers.";
                else if (rank == 50000.0f) text = " feedings fill me with immense strength and power.";
                else if (rank >= 60000.0f) text = " feedings grant me immeasurable power and status among Vampires.";
                if (!text) return;

                std::string msg = std::to_string(a_result.GetSInt()) + text;
                RE::DebugNotification(msg.c_str());
            }
            bool CanSave() const override { return false; }
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
        };

        // Dispatched after IncrementStat so the count reflects this feed
        void ShowRankFeedMessage() {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return;

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new RankFeedNotifyCallback());
            RE::BSFixedString stat("Necks Bitten");
            vm->DispatchStaticCall("Game", "QueryStat", RE::MakeFunctionArguments(std::move(stat)), callback);
        }

        // Live "Necks Bitten" game stat for the debug UI. QueryStat is async, so a callback caches
        // the last value and GetHungerDebug re-queries on a throttle.
        std::atomic<int> g_necksBittenStat{-1};

        class NecksBittenStatCallback : public RE::BSScript::IStackCallbackFunctor {
        public:
            void operator()(RE::BSScript::Variable a_result) override {
                if (a_result.IsInt()) g_necksBittenStat = a_result.GetSInt();
            }
            bool CanSave() const override { return false; }
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
        };

        void RefreshNecksBittenStat() {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return;
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb(new NecksBittenStatCallback());
            RE::BSFixedString stat("Necks Bitten");
            vm->DispatchStaticCall("Game", "QueryStat", RE::MakeFunctionArguments(std::move(stat)), cb);
        }

        // Papyrus IsInLocation semantics: current location or any parent matches
        bool PlayerIsInAnyLocation(RE::PlayerCharacter* player, RE::BGSLocation* const* locations, size_t count) {
            for (auto* current = player->GetCurrentLocation(); current; current = current->parentLoc) {
                for (size_t i = 0; i < count; ++i) {
                    if (locations[i] && current == locations[i]) return true;
                }
            }
            return false;
        }

        // Walks parent types too (RegisterForUpdateGameTime lives on the Form script)
        bool TypeHasFunction(RE::BSScript::ObjectTypeInfo* typeInfo, const char* name) {
            for (auto* cls = typeInfo; cls; cls = cls->GetParent()) {
                auto* funcs = cls->GetMemberFuncIter();
                for (uint32_t i = 0; funcs && i < cls->GetNumMemberFuncs(); ++i) {
                    auto* func = funcs[i].func.get();
                    if (func && func->GetName() == name) return true;
                }
            }
            return false;
        }

        // One-time check at first feed (the script only binds in a running save):
        // BV ships no version number anywhere, so classify by script shape -
        // TurnedNPCRefresh() was added to PlayerVampireQuestScript in 9.1 and the
        // deep integration is written against 9.1 feed logic.
        bool VerifyScriptShape() {
            if (g_shapeVerified) return true;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, g_playerVampireQuest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            RE::BSTSmartPointer<RE::BSScript::Object> obj;
            if (!vm->FindBoundObject(handle, kScriptName, obj) || !obj) {
                SKSE::log::warn("BetterVampiresIntegration: {} not bound - retrying next feed", kScriptName);
                return false;
            }

            auto* typeInfo = obj->GetTypeInfo();
            if (!typeInfo || !TypeHasFunction(typeInfo, "TurnedNPCRefresh")) {
                g_versionInfo = "8.9 or older (deep integration disabled)";
                SKSE::log::warn("BetterVampiresIntegration: detected Better Vampires 8.9 or older "
                    "(or another mod overrides PlayerVampireQuestScript.pex) - deep integration disabled, using Papyrus path");
                RE::DebugNotification("Better Vampires 8.9 or older detected - deep feed integration disabled");
                g_available = false;
                return false;
            }
            g_versionInfo = "9.1+";
            SKSE::log::info("BetterVampiresIntegration: detected Better Vampires 9.1+");

            // Dispatched Papyrus functions - a renamed/absent one fails silently at feed time
            constexpr const char* kDispatchedFuncs[] = {
                "AmaranthGainSkills", "TurnNPCIntoVampire",
                "NormalRankProgression", "EasierRankProgression", "DaysAsVampireProgression",
                "SpecialFeedingBonus",
                "TwoStagesSatiation", "DynamicStagesSatiation", "NormalStagesSatiation",
                "RegisterForUpdateGameTime",
            };
            for (const char* funcName : kDispatchedFuncs) {
                SKSE::log::debug("  {}: {}", funcName, TypeHasFunction(typeInfo, funcName) ? "found" : "missing");
            }

            // MAGVampireTransform01 is a SOUN record - editor-ID lookup misses it, so read
            // the value straight off the script property and unwrap to a descriptor
            if (!g_feedSound) {
                if (auto* var = obj->GetProperty("MAGVampireTransform01"); var && var->IsObject()) {
                    if (auto* soun = var->Unpack<RE::TESSound*>()) {
                        g_feedSoundRecord = soun;
                        g_feedSound = soun->descriptor;
                    }
                }
                SKSE::log::debug("  MAGVampireTransform01 (via property): {}", g_feedSound ? "found" : "missing");
            }

            g_shapeVerified = true;
            return true;
        }
    }

    bool Initialize() {
        if (g_initialized) {
            return g_available;
        }
        g_initialized = true;

        SKSE::log::info("BetterVampiresIntegration: Initializing...");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::warn("BetterVampiresIntegration: DataHandler not available");
            return false;
        }

        // Check for Better Vampires ESP
        bool hasBetterVampires = dataHandler->LookupModByName("Better Vampires.esp") != nullptr;
        if (!hasBetterVampires) {
            SKSE::log::info("BetterVampiresIntegration: Better Vampires not installed");
            g_available = false;
            return false;
        }

        SKSE::log::info("BetterVampiresIntegration: Better Vampires ESP detected, looking up forms...");

        // Globals - Better Vampires specific
        g_usingBVScripts = RE::TESForm::LookupByEditorID<RE::TESGlobal>("UsingBetterVampiresScripts");
        g_menuSpellToggle = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireMenuSpell");
        g_bottledBlood = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireBottledBlood");
        g_extractingBlood = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireExtractingBlood");
        g_bloodPoints = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireBloodPoints");
        g_enableBloodPoints = RE::TESForm::LookupByEditorID<RE::TESGlobal>("EnableVampireBloodPoints");
        g_dynamicStages = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireDynamicStages");
        g_feedOffDead = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireFeedOffDead");
        g_createVampire = RE::TESForm::LookupByEditorID<RE::TESGlobal>("CreateVampire");
        g_vampireRank = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireRank");
        g_engorge = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireEngorge");
        g_engorgeAmount = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireEngorgeAmount");
        g_extractBloodToggle = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireExtractBlood");
        g_necksBittenDiscovered = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireNecksBittenDiscovered");
        g_rankProgression = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireRankProgression");
        g_statusMessages = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireStatusMessages");
        g_specialVictimFeeding = RE::TESForm::LookupByEditorID<RE::TESGlobal>("BVSpecialVictimFeeding");
        g_lastTimeFed = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireLastTimeFed");
        g_skillPointsTotal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("BVMCMSkillPointsTotal");
        g_skillPointsAvailable = RE::TESForm::LookupByEditorID<RE::TESGlobal>("BVMCMSkillPointsAvailable");
        g_giveAllSkillPoints = RE::TESForm::LookupByEditorID<RE::TESGlobal>("BVMCMGiveAllSkillPointsGlobal");
        g_neckMarksToggle = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireNeckMarks");
        g_targetAlreadyDeadGlobal = RE::TESForm::LookupByEditorID<RE::TESGlobal>("TargetAlreadyDeadGlobal");
        g_noRedScreen = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireNoRedScreen");
        g_calcFeedTimer = RE::TESForm::LookupByEditorID<RE::TESGlobal>("BVCalculateFeedTimer");
        g_updateGameTimeGate = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireUpdateGameTime");

        // Globals - vanilla (shared)
        g_feedReady = RE::TESForm::LookupByEditorID<RE::TESGlobal>("VampireFeedReady");
        g_gameDaysPassed = RE::TESForm::LookupByEditorID<RE::TESGlobal>("GameDaysPassed");

        // Quests
        g_playerVampireQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("PlayerVampireQuest");

        // Factions
        g_vampirePCFamily = RE::TESForm::LookupByEditorID<RE::TESFaction>("VampirePCFamily");

        // Keywords
        g_vampireKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("Vampire");

        // Spells
        g_menuOptionsSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("BetterVampiresMenuOptionsSpell");
        g_victimDamageSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("VampireVictimDamage2");
        g_bleedingSpell = RE::TESForm::LookupByEditorID<RE::SpellItem>("BleedingSpell");

        // Perks
        g_extractBloodPerk = RE::TESForm::LookupByEditorID<RE::BGSPerk>("VampireExtractBloodPotions");

        // Sounds - MAGVampireTransform01 (SOUN 0x000FF9E9) / MAGVampireTransform01SD
        // (SNDR 0x000FF9E8) are vanilla Skyrim.esm forms whose editor IDs aren't cached,
        // so resolve by FormID. PlaySound needs the descriptor (SNDR).
        g_feedSoundRecord = RE::TESForm::LookupByID<RE::TESSound>(0x000FF9E9);
        g_feedSound = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x000FF9E8);
        if (!g_feedSound && g_feedSoundRecord) g_feedSound = g_feedSoundRecord->descriptor;

        // Effect shaders
        g_neckMarksShader = RE::TESForm::LookupByEditorID<RE::TESEffectShader>("NeckMarksRight");

        // Image space modifiers
        g_redScreenISM = RE::TESForm::LookupByEditorID<RE::TESImageSpaceModifier>("VampireTransformDecreaseISMD");

        // FormLists
        g_powerfulVictims = RE::TESForm::LookupByEditorID<RE::BGSListForm>("BVPowerfulFeedingVictims");

        // Locations
        for (size_t i = 0; i < std::size(kCityLocationIDs); ++i) {
            g_cityLocations[i] = RE::TESForm::LookupByEditorID<RE::BGSLocation>(kCityLocationIDs[i]);
        }
        for (size_t i = 0; i < std::size(kTownLocationIDs); ++i) {
            g_townLocations[i] = RE::TESForm::LookupByEditorID<RE::BGSLocation>(kTownLocationIDs[i]);
        }

        // Validate essential forms are present
        g_available = g_playerVampireQuest && g_usingBVScripts;
        if (!g_available) {
            SKSE::log::warn("BetterVampiresIntegration: Missing essential forms - PlayerVampireQuest:{}, UsingBetterVampiresScripts:{}",
                g_playerVampireQuest ? "ok" : "MISSING",
                g_usingBVScripts ? "ok" : "MISSING");
            return false;
        }
        g_versionInfo = "unknown (version check runs at first feed)";
        SKSE::log::debug("BetterVampiresIntegration: Initialized successfully");

        // Globals - Better Vampires specific
        SKSE::log::debug("  UsingBetterVampiresScripts: {}", g_usingBVScripts ? "found" : "missing");
        SKSE::log::debug("  VampireMenuSpell: {}", g_menuSpellToggle ? "found" : "missing");
        SKSE::log::debug("  VampireBottledBlood: {}", g_bottledBlood ? "found" : "missing");
        SKSE::log::debug("  VampireExtractingBlood: {}", g_extractingBlood ? "found" : "missing");
        SKSE::log::debug("  VampireBloodPoints: {}", g_bloodPoints ? "found" : "missing");
        SKSE::log::debug("  EnableVampireBloodPoints: {}", g_enableBloodPoints ? "found" : "missing");
        SKSE::log::debug("  VampireDynamicStages: {}", g_dynamicStages ? "found" : "missing");
        SKSE::log::debug("  VampireFeedOffDead: {}", g_feedOffDead ? "found" : "missing");
        SKSE::log::debug("  CreateVampire: {}", g_createVampire ? "found" : "missing");
        SKSE::log::debug("  VampireRank: {}", g_vampireRank ? "found" : "missing");
        SKSE::log::debug("  VampireEngorge: {}", g_engorge ? "found" : "missing");
        SKSE::log::debug("  VampireEngorgeAmount: {}", g_engorgeAmount ? "found" : "missing");
        SKSE::log::debug("  VampireExtractBlood: {}", g_extractBloodToggle ? "found" : "missing");
        SKSE::log::debug("  VampireNecksBittenDiscovered: {}", g_necksBittenDiscovered ? "found" : "missing");
        SKSE::log::debug("  VampireRankProgression: {}", g_rankProgression ? "found" : "missing");
        SKSE::log::debug("  VampireStatusMessages: {}", g_statusMessages ? "found" : "missing");
        SKSE::log::debug("  BVSpecialVictimFeeding: {}", g_specialVictimFeeding ? "found" : "missing");
        SKSE::log::debug("  VampireLastTimeFed: {}", g_lastTimeFed ? "found" : "missing");
        SKSE::log::debug("  BVMCMSkillPointsTotal: {}", g_skillPointsTotal ? "found" : "missing");
        SKSE::log::debug("  BVMCMSkillPointsAvailable: {}", g_skillPointsAvailable ? "found" : "missing");
        SKSE::log::debug("  BVMCMGiveAllSkillPointsGlobal: {}", g_giveAllSkillPoints ? "found" : "missing");
        SKSE::log::debug("  VampireNeckMarks: {}", g_neckMarksToggle ? "found" : "missing");
        SKSE::log::debug("  TargetAlreadyDeadGlobal: {}", g_targetAlreadyDeadGlobal ? "found" : "missing");
        SKSE::log::debug("  VampireNoRedScreen: {}", g_noRedScreen ? "found" : "missing");
        SKSE::log::debug("  BVCalculateFeedTimer: {}", g_calcFeedTimer ? "found" : "missing");
        SKSE::log::debug("  VampireUpdateGameTime: {}", g_updateGameTimeGate ? "found" : "missing");

        // Globals - vanilla
        SKSE::log::debug("  VampireFeedReady: {}", g_feedReady ? "found" : "missing");
        SKSE::log::debug("  GameDaysPassed: {}", g_gameDaysPassed ? "found" : "missing");

        // Quests
        SKSE::log::debug("  PlayerVampireQuest: {}", g_playerVampireQuest ? "found" : "missing");

        // Factions
        SKSE::log::debug("  VampirePCFamily: {}", g_vampirePCFamily ? "found" : "missing");

        // Keywords
        SKSE::log::debug("  Vampire: {}", g_vampireKeyword ? "found" : "missing");

        // Spells
        SKSE::log::debug("  BetterVampiresMenuOptionsSpell: {}", g_menuOptionsSpell ? "found" : "missing");
        SKSE::log::debug("  VampireVictimDamage2: {}", g_victimDamageSpell ? "found" : "missing");
        SKSE::log::debug("  BleedingSpell: {}", g_bleedingSpell ? "found" : "missing");

        // Perks
        SKSE::log::debug("  VampireExtractBloodPotions: {}", g_extractBloodPerk ? "found" : "missing");

        // Sounds
        SKSE::log::debug("  MAGVampireTransform01 (SOUN): {}", g_feedSoundRecord ? "found" : "missing");
        SKSE::log::debug("  MAGVampireTransform01SD (descriptor): {}", g_feedSound ? "found" : "missing");

        // Effect shaders
        SKSE::log::debug("  NeckMarksRight: {}", g_neckMarksShader ? "found" : "missing");

        // Image space modifiers
        SKSE::log::debug("  VampireTransformDecreaseISMD: {}", g_redScreenISM ? "found" : "missing");

        // FormLists
        SKSE::log::debug("  BVPowerfulFeedingVictims: {}", g_powerfulVictims ? "found" : "missing");

        // Locations
        for (size_t i = 0; i < std::size(kCityLocationIDs); ++i) {
            SKSE::log::debug("  {}: {}", kCityLocationIDs[i], g_cityLocations[i] ? "found" : "missing");
        }
        for (size_t i = 0; i < std::size(kTownLocationIDs); ++i) {
            SKSE::log::debug("  {}: {}", kTownLocationIDs[i], g_townLocations[i] ? "found" : "missing");
        }

        return true;
    }

    bool IsAvailable() {
        if (!g_initialized) {
            Initialize();
        }
        return g_available;
    }

    const char* GetVersionInfo() {
        return g_versionInfo.load();
    }

    int GetNecksBittenStat() {
        // Base-game stat, so query independent of BV availability. Throttle so the async
        // QueryStat isn't re-dispatched every frame the debug UI renders.
        using clock = std::chrono::steady_clock;
        static clock::time_point lastQuery{};
        auto now = clock::now();
        if (now - lastQuery > std::chrono::milliseconds(500)) {
            lastQuery = now;
            RefreshNecksBittenStat();
        }
        return g_necksBittenStat.load();
    }

    HungerDebug GetHungerDebug() {
        HungerDebug d;
        if (!g_available || !g_playerVampireQuest) return d;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return d;
        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, g_playerVampireQuest);
        if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return d;
        RE::BSTSmartPointer<RE::BSScript::Object> obj;
        if (!vm->FindBoundObject(handle, kScriptName, obj) || !obj) return d;

        // VampireStatus is an Int property (not Float) - accept both so it doesn't read as the fallback
        auto readFloatProp = [&](const char* prop, float fallback) -> float {
            auto* var = obj->GetProperty(prop);
            if (var) {
                if (var->IsFloat()) return var->GetFloat();
                if (var->IsInt()) return static_cast<float>(var->GetSInt());
            }
            return fallback;
        };

        d.valid = true;
        d.bloodPointsMode = g_enableBloodPoints && g_enableBloodPoints->value == 10000.0f;
        d.stageMode = g_dynamicStages ? static_cast<int>(g_dynamicStages->value) : 0;
        d.feedReady = g_feedReady ? g_feedReady->value : -1.0f;
        d.bloodPoints = g_bloodPoints ? g_bloodPoints->value : -1.0f;
        d.gameDaysPassed = g_gameDaysPassed ? g_gameDaysPassed->value : -1.0f;
        d.feedTimerEnabled = g_calcFeedTimer && g_calcFeedTimer->value > 0.0f;
        d.updateGated = g_updateGameTimeGate && g_updateGameTimeGate->value != 0.0f;
        d.vampireStatus = readFloatProp("VampireStatus", -1.0f);
        d.feedTimer = readFloatProp("FeedTimer", -1.0f);
        d.lastFeedTime = readFloatProp("LastFeedTime", -1.0f);

        // Necks Bitten: the vanilla feed-count stat (async, cached) plus BV's separate notoriety/rank globals
        d.necksBitten = GetNecksBittenStat();
        d.necksBittenDiscovered = g_necksBittenDiscovered ? g_necksBittenDiscovered->value : -1.0f;
        d.vampireRank = g_vampireRank ? g_vampireRank->value : -1.0f;
        return d;
    }

    bool ProcessFeed(const FeedContext& context) {
        if (!context.target) {
            SKSE::log::error("BetterVampiresIntegration::ProcessFeed: target is null");
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("BetterVampiresIntegration::ProcessFeed: player is null");
            return false;
        }

        // 8.9-or-older script shape disables the deep path (checked once, needs a running save)
        if (!VerifyScriptShape()) {
            return false;
        }

        SKSE::log::info("BetterVampiresIntegration::ProcessFeed: target={}, lethal={}, combat={}, sleeping={}",
            context.target->GetName(), context.isLethal, context.isCombatFeed, context.isSleeping);

        // Full pre-feed hunger snapshot (save values, valid now the game is running). Comparing
        // FeedTimer / VampireStatus across consecutive feeds shows whether hunger advanced while
        // waiting - a frozen FeedTimer after a long wait is the smoking gun.
        {
            HungerDebug h = GetHungerDebug();
            SKSE::log::info("BetterVampiresIntegration: hunger snapshot - mode={}, stageMode={}, VampireFeedReady={:.0f}, "
                "VampireStatus={:.0f}, VampireBloodPoints={:.0f}, FeedTimer={:.3f}, LastFeedTime={:.3f}, GameDaysPassed={:.3f}, "
                "feedTimerEnabled={}, updateGated={}",
                h.bloodPointsMode ? "BloodPoints" : "FeedTimer", h.stageMode, h.feedReady,
                h.vampireStatus, h.bloodPoints, h.feedTimer, h.lastFeedTime, h.gameDaysPassed,
                h.feedTimerEnabled, h.updateGated);
        }

        auto* targetAV = context.target->AsActorValueOwner();
        auto* playerAV = player->AsActorValueOwner();

        // A lethal feed's victim may already be dead when the animation handled the kill,
        // so "corpse feed" means dead AND not our kill.
        const bool wasCorpseFeed = context.target->IsDead() && !context.isLethal;
        const bool isVampireVictim = g_vampireKeyword && context.target->HasKeyword(g_vampireKeyword);

        // === STEP 1: Feeding-in-progress flag (BV leaves it at 3 after VampireFeed) ===
        if (g_usingBVScripts) g_usingBVScripts->value = 3.0f;

        // === STEP 2: Menu spell housekeeping ===
        if (g_menuSpellToggle && g_menuOptionsSpell) {
            if (g_menuSpellToggle->value == 0.0f) {
                if (!player->HasSpell(g_menuOptionsSpell)) player->AddSpell(g_menuOptionsSpell);
            } else if (g_menuSpellToggle->value == 10000.0f) {
                if (player->HasSpell(g_menuOptionsSpell)) player->RemoveSpell(g_menuOptionsSpell);
            }
        }

        // === STEP 3: Player joins the vampire family faction ===
        if (g_vampirePCFamily) player->AddToFaction(g_vampirePCFamily, 0);

        // === STEPS 4-7: controls / vanilla anims / assault alarm / sneak minigame - SKIP ===

        // === STEP 8: Blood points (per-route values folded to dead/alive x mortal/vampire) ===
        const bool bloodPointsAllowed = g_bloodPoints &&
            g_enableBloodPoints && g_enableBloodPoints->value <= 10000.0f &&
            (!g_createVampire || g_createVampire->value == 0.0f) &&
            (!g_extractingBlood || g_extractingBlood->value == 0.0f) &&
            (!wasCorpseFeed || (g_feedOffDead && g_feedOffDead->value == 10000.0f));
        if (bloodPointsAllowed) {
            const float amount = wasCorpseFeed ? (isVampireVictim ? 75.0f : 50.0f)
                                               : (isVampireVictim ? 150.0f : 100.0f);
            const float cap = (g_dynamicStages && g_dynamicStages->value == 20000.0f) ? 100.0f : 300.0f;
            g_bloodPoints->value = std::min(g_bloodPoints->value + amount, cap);
            SKSE::log::info("BetterVampiresIntegration: +{:.0f} blood points ({:.0f}/{:.0f})", amount, g_bloodPoints->value, cap);
        }

        // === STEP 9: Victim marking + double-feed drain protection ===
        if (wasCorpseFeed) {
            if (g_targetAlreadyDeadGlobal) g_targetAlreadyDeadGlobal->value = 1.0f;
            targetAV->SetActorValue(RE::ActorValue::kVariable08, 9.0f);
        } else {
            // Variable08: 10 = being restored by BV's victim script, 11 = fed on within its
            // 22s window (a second feed drains the victim dry), 9 = spent
            const float var08 = targetAV->GetActorValue(RE::ActorValue::kVariable08);
            if (var08 == 10.0f) {
                targetAV->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, 1000000.0f);
            } else if (var08 == 11.0f) {
                targetAV->SetActorValue(RE::ActorValue::kVariable08, 9.0f);
                targetAV->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -1000000.0f);
                if (context.target->IsEssential()) {
                    context.target->KillImpl(player, 1000.0f, true, true);
                    RE::DebugNotification("I have overfed on this essential mortal ...");
                    RE::DebugNotification("They are of no use to me now.");
                }
                SKSE::log::info("BetterVampiresIntegration: Overfed victim within protection window - drained dry");
            }
        }

        // === STEP 10: Lethal kill ===
        if (context.isLethal) {
            if (!context.animationHandlesKill && !context.target->IsDead()) {
                context.target->KillImpl(player, 1000.0f, true, true);
                SKSE::log::info("BetterVampiresIntegration: Killed target");
            }
            if (context.target->IsEssential()) {
                RE::DebugNotification("I have overfed on this essential mortal ...");
                RE::DebugNotification("They are of no use to me now.");
            }
        }

        // === STEP 11: Cosmetics ===
        if (g_feedSound) PlaySound(g_feedSound, context.target);
        if (!wasCorpseFeed && g_neckMarksToggle && g_neckMarksToggle->value == 0.0f && g_neckMarksShader) {
            context.target->ApplyEffectShader(g_neckMarksShader, 240.0f);
        }
        if (g_bleedingSpell && context.target->IsDead()) {
            CastSpell(g_bleedingSpell, context.target, context.target);
        }
        TriggerScreenBlood(3);

        // === STEP 12: Amaranth skill absorption (vampire victims) ===
        if (isVampireVictim) {
            CallPapyrusMethod(g_playerVampireQuest, kScriptName, "AmaranthGainSkills");
        }

        // === STEP 13: Praestare Sanguinare turns the victim instead of the feed payout ===
        const bool turningVictim = g_createVampire && g_createVampire->value > 0.0f && !isVampireVictim &&
            (!g_bottledBlood || g_bottledBlood->value == 0.0f) &&
            (!g_extractingBlood || g_extractingBlood->value == 0.0f);
        if (turningVictim) {
            CallBVMethod("TurnNPCIntoVampire", context.target);
            SKSE::log::info("BetterVampiresIntegration: Turning victim into a vampire");
        } else {
            // === STEP 14: Red screen crossfade (BV gates it on VampireNoRedScreen) ===
            if (!g_noRedScreen || g_noRedScreen->value == 0.0f) {
                ApplyRedScreenCrossFade();
            }

            // === STEP 15: Extract-blood perk sync ===
            if (g_extractBloodPerk && g_extractBloodToggle) {
                if (g_extractBloodToggle->value == 10000.0f) player->AddPerk(g_extractBloodPerk);
                else if (g_extractBloodToggle->value == 0.0f) player->RemovePerk(g_extractBloodPerk);
            }

            // === STEP 16: Victim drain (25% of current health) + victim-side spell ===
            const float victimHealth = targetAV->GetActorValue(RE::ActorValue::kHealth);
            if (victimHealth > 0.0f) {
                targetAV->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -(victimHealth * 0.25f));
                if (g_victimDamageSpell) {
                    context.target->RemoveSpell(g_victimDamageSpell);
                    context.target->AddSpell(g_victimDamageSpell);
                }
            }

            // === STEP 17: Player restore + Engorge (rank >= 60000 raises max stats) ===
            {
                const bool engorgeReady = g_vampireRank && g_vampireRank->value >= 60000.0f &&
                                          g_engorge && g_engorge->value == 0.0f;
                float restoreHealth, restoreOther, engorgeGain;
                bool engorgeAllowed;
                if (isVampireVictim) {
                    restoreHealth = 150.0f; restoreOther = 75.0f; engorgeGain = 1.0f;
                    engorgeAllowed = engorgeReady;
                } else if (!wasCorpseFeed) {
                    restoreHealth = 100.0f; restoreOther = 50.0f; engorgeGain = 0.5f;
                    engorgeAllowed = engorgeReady;
                } else {
                    restoreHealth = 50.0f; restoreOther = 25.0f; engorgeGain = 0.5f;
                    engorgeAllowed = engorgeReady && g_feedOffDead && g_feedOffDead->value == 10000.0f;
                }
                if (engorgeAllowed) {
                    if (g_engorgeAmount) g_engorgeAmount->value += engorgeGain;
                    playerAV->ModActorValue(RE::ActorValue::kHealth, engorgeGain);
                    playerAV->ModActorValue(RE::ActorValue::kStamina, engorgeGain);
                    playerAV->ModActorValue(RE::ActorValue::kMagicka, engorgeGain);
                }
                playerAV->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, restoreHealth);
                playerAV->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, restoreOther);
                playerAV->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, restoreOther);
                SKSE::log::info("BetterVampiresIntegration: Restored {:.0f}/{:.0f}/{:.0f} H/S/M{}",
                    restoreHealth, restoreOther, restoreOther, engorgeAllowed ? " (+Engorge)" : "");
            }

            // Corpse feeds only count when feeding off dead is allowed or the corpse was a
            // bled-out victim (Variable05 == 9)
            const bool fedSuccessfully = !wasCorpseFeed ||
                (g_feedOffDead && g_feedOffDead->value == 10000.0f) ||
                targetAV->GetActorValue(RE::ActorValue::kVariable05) == 9.0f;

            if (fedSuccessfully) {
                // === STEP 18: Necks Bitten stat + discovery counter by location ===
                IncrementNecksBittenStat();
                if (g_necksBittenDiscovered) {
                    float add = 0.5f;
                    if (PlayerIsInAnyLocation(player, g_cityLocations, std::size(g_cityLocations))) add = 2.0f;
                    else if (PlayerIsInAnyLocation(player, g_townLocations, std::size(g_townLocations))) add = 1.0f;
                    g_necksBittenDiscovered->value += add;
                }

                // === STEP 19: Rank progression (mode-selected, stays in Papyrus) ===
                if (g_rankProgression) {
                    const float mode = g_rankProgression->value;
                    if (mode == 0.0f) CallPapyrusMethod(g_playerVampireQuest, kScriptName, "NormalRankProgression");
                    else if (mode == 10000.0f) CallPapyrusMethod(g_playerVampireQuest, kScriptName, "EasierRankProgression");
                    else if (mode == 20000.0f) CallPapyrusMethod(g_playerVampireQuest, kScriptName, "DaysAsVampireProgression");
                }

                // === STEP 20: Rank status notification ===
                if (g_statusMessages && g_statusMessages->value == 0.0f &&
                    g_rankProgression && g_rankProgression->value != 20000.0f) {
                    ShowRankFeedMessage();
                }
            }

            // === STEP 21: Special bonus for famous victims ===
            if (g_specialVictimFeeding && g_specialVictimFeeding->value == 10000.0f && g_powerfulVictims) {
                auto* actorBase = context.target->GetActorBase();
                if (actorBase && g_powerfulVictims->HasForm(actorBase)) {
                    CallBVMethod("SpecialFeedingBonus", context.target);
                    SKSE::log::info("BetterVampiresIntegration: Special feeding bonus for {}", context.target->GetName());
                }
            }

            if (fedSuccessfully) {
                // === STEP 22: Ready to feed again ===
                if (g_feedReady) g_feedReady->value = 0.0f;

                // === STEP 23: Satiation stage (mode-selected, stays in Papyrus) + feed time ===
                if (g_dynamicStages) {
                    const float mode = g_dynamicStages->value;
                    if (mode == 20000.0f) CallPapyrusMethod(g_playerVampireQuest, kScriptName, "TwoStagesSatiation");
                    else if (mode == 10000.0f) CallPapyrusMethod(g_playerVampireQuest, kScriptName, "DynamicStagesSatiation");
                    else if (mode == 0.0f) CallPapyrusMethod(g_playerVampireQuest, kScriptName, "NormalStagesSatiation");
                }
                if (g_lastTimeFed && g_gameDaysPassed) g_lastTimeFed->value = g_gameDaysPassed->value;
            }
        }

        // === STEP 24: Spent victims can't be fed on again (lethal included - the
        // animation-handled kill may land after this runs) ===
        if (context.target->IsDead() || context.isLethal) {
            targetAV->SetActorValue(RE::ActorValue::kVariable08, 9.0f);
        }

        // === STEPS 25-26: player controls / sneak fear reset - SKIP ===

        // === STEP 27: Skill point chance (rolls 1 and 99 of 0-100) ===
        if (g_skillPointsTotal && g_skillPointsAvailable) {
            const bool capped = g_skillPointsTotal->value >= 26.0f ||
                                (g_giveAllSkillPoints && g_giveAllSkillPoints->value == 1.0f);
            if (!capped) {
                const int roll = rand() % 101;
                if (roll == 1 || roll == 99) {
                    g_skillPointsTotal->value += 1.0f;
                    g_skillPointsAvailable->value += 1.0f;
                    RE::DebugNotification("1 Skill Point Earned.");
                    SKSE::log::info("BetterVampiresIntegration: Skill point earned");
                }
            }
        }

        // === STEP 28: Reset bottled/extracting flags ===
        if (g_bottledBlood) g_bottledBlood->value = 0.0f;
        if (g_extractingBlood) g_extractingBlood->value = 0.0f;

        // === STEP 29: Re-arm the hunger tick from a full stomach. RegisterForUpdateGameTime
        // replaces any existing registration, so it both resets the timer and (re)enables it -
        // no separate unregister. BV's Papyrus unregisters first, but dispatched async that
        // call could land AFTER this one and cancel the tick, freezing hunger progression. ===
        const bool reReg = CallBVMethod("RegisterForUpdateGameTime", 1.0f);
        SKSE::log::debug("BetterVampiresIntegration: RegisterForUpdateGameTime dispatch {}", reReg ? "ok" : "FAILED");

        SKSE::log::info("BetterVampiresIntegration::ProcessFeed: Complete");
        return true;
    }
}
