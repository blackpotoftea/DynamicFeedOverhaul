#include "PCH.h"
#include "BetterVampiresIntegration.h"
#include "VampireIntegrationUtils.h"

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
 *  7   | Route: sneak escape minigame (20/20/60)    | SKIP - player decides via prompt
 *  8   | Route: blood points (+50/+75/+100/+150)    | ✅ DONE
 *  9   | Route: victim mark Variable08/05, drain    | ✅ DONE
 * 10   | Route: lethal drain kill + essential msgs  | ✅ DONE
 * 11   | Route: feed sound / BleedingSpell /        | ✅ DONE
 *      |   NeckMarksRight shader / screen blood     |
 * 12   | AmaranthGainSkills (vampire victims)       | ✅ DONE - Papyrus dispatch
 * 13   | TurnNPCIntoVampire (CreateVampire > 0)     | ✅ DONE - Papyrus dispatch; ELSE-gates 14-23
 * 14   | Red screen ISM crossfade                   | SKIP - blocking + purely cosmetic
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
 * 26   | Sneak victim 15s fear reset                | SKIP - tied to skipped minigame
 * 27   | Skill point chance (2/100)                 | ✅ DONE
 * 28   | Reset BottledBlood/ExtractingBlood globals | ✅ DONE
 * 29   | Re-register UpdateGameTime (hunger tick)   | ✅ DONE - Papyrus dispatch
 *
 * =============================================================================
 */

namespace BetterVampiresIntegration {

    namespace {
        constexpr const char* kScriptName = "PlayerVampireQuestScript";

        std::atomic<bool> g_initialized{false};
        std::atomic<bool> g_available{false};
        std::atomic<bool> g_propsResolved{false};

        RE::TESQuest* g_playerVampireQuest = nullptr;

        // Globals (resolved from PlayerVampireQuestScript properties)
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

        // Forms (resolved from PlayerVampireQuestScript properties)
        RE::TESFaction* g_vampirePCFamily = nullptr;
        RE::BGSKeyword* g_vampireKeyword = nullptr;
        RE::SpellItem* g_menuOptionsSpell = nullptr;
        RE::SpellItem* g_victimDamageSpell = nullptr;
        RE::SpellItem* g_bleedingSpell = nullptr;
        RE::BGSPerk* g_extractBloodPerk = nullptr;
        RE::BGSSoundDescriptorForm* g_feedSound = nullptr;
        RE::TESEffectShader* g_neckMarksShader = nullptr;
        RE::BGSListForm* g_powerfulVictims = nullptr;

        // Necks-bitten discovery weights: +2 in a city, +1 in a town, +0.5 elsewhere
        constexpr const char* kCityLocationProps[] = {
            "DawnStarLocation", "MarkarthLocation", "MorthalLocation", "RiftenLocation", "SolitudeLocation",
            "WhiterunLocation", "WindhelmLocation", "WinterholdLocation", "FalkreathLocation"
        };
        constexpr const char* kTownLocationProps[] = {
            "DragonBridgeLocation", "HelgenLocation", "IvarsteadLocation", "KarthwastenLocation", "RiverwoodLocation",
            "RoriksteadLocation", "ShorsStoneLocation", "RavenRockLocation", "SkaalVillageLocation"
        };
        RE::BGSLocation* g_cityLocations[std::size(kCityLocationProps)]{};
        RE::BGSLocation* g_townLocations[std::size(kTownLocationProps)]{};

        using VampireIntegrationUtils::PlaySound;
        using VampireIntegrationUtils::CastSpell;
        using VampireIntegrationUtils::CallPapyrusMethod;

        int g_unresolvedProps = 0;

        // Read a form-typed property off the bound quest script; property names are
        // exact from the .psc, so no EditorID guessing is needed.
        template <class T>
        void ResolveProperty(const RE::BSTSmartPointer<RE::BSScript::Object>& obj, const char* prop, T*& out) {
            auto* var = obj->GetProperty(prop);
            out = (var && var->IsObject()) ? var->Unpack<T*>() : nullptr;
            if (out) {
                // FormID + name of the actual filled value, for manual comparison against the CK
                SKSE::log::debug("  BV property {}: 0x{:08X} '{}'", prop, out->GetFormID(), out->GetName());
            } else {
                ++g_unresolvedProps;
                // "empty (None)" = stale save instance (script bound before BV was installed);
                // "not on script" = another mod's PlayerVampireQuestScript.pex won the conflict
                SKSE::log::debug("  BV property {}: {}", prop, var ? "empty (None)" : "not on script");
            }
        }

        // Properties resolve lazily on first feed: the quest script is only
        // guaranteed to be bound once a save is running, not at kDataLoaded.
        bool ResolveScriptProperties() {
            if (g_propsResolved) return true;
            if (!g_playerVampireQuest) return false;

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::TESQuest::FORMTYPE, g_playerVampireQuest);
            if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return false;

            RE::BSTSmartPointer<RE::BSScript::Object> obj;
            if (!vm->FindBoundObject(handle, kScriptName, obj) || !obj) {
                SKSE::log::warn("BetterVampiresIntegration: {} not bound - cannot resolve properties", kScriptName);
                return false;
            }

            // BV ships no version number anywhere, so classify by script shape:
            // TurnedNPCRefresh() was added to PlayerVampireQuestScript in 9.1. The deep
            // integration is written against 9.1 feed logic - disable it for older shapes.
            bool has91Shape = false;
            if (auto* typeInfo = obj->GetTypeInfo()) {
                for (uint32_t i = 0; i < typeInfo->GetNumMemberFuncs(); ++i) {
                    auto* func = typeInfo->GetMemberFuncIter()[i].func.get();
                    if (func && func->GetName() == "TurnedNPCRefresh") {
                        has91Shape = true;
                        break;
                    }
                }
            }
            if (!has91Shape) {
                SKSE::log::warn("BetterVampiresIntegration: script shape matches Better Vampires 8.9 or older "
                    "(or another mod overrides PlayerVampireQuestScript.pex) - deep integration disabled, using Papyrus path");
                RE::DebugNotification("Better Vampires 8.9 or older detected - deep feed integration disabled");
                g_available = false;
                return false;
            }
            SKSE::log::info("BetterVampiresIntegration: script shape matches Better Vampires 9.1+");

            SKSE::log::debug("BetterVampiresIntegration: resolving script properties...");
            g_unresolvedProps = 0;

            ResolveProperty(obj, "UsingBetterVampiresScripts", g_usingBVScripts);
            ResolveProperty(obj, "VampireMenuSpell", g_menuSpellToggle);
            ResolveProperty(obj, "VampireBottledBlood", g_bottledBlood);
            ResolveProperty(obj, "VampireExtractingBlood", g_extractingBlood);
            ResolveProperty(obj, "VampireFeedReady", g_feedReady);
            ResolveProperty(obj, "VampireBloodPoints", g_bloodPoints);
            ResolveProperty(obj, "EnableVampireBloodPoints", g_enableBloodPoints);
            ResolveProperty(obj, "VampireDynamicStages", g_dynamicStages);
            ResolveProperty(obj, "VampireFeedOffDead", g_feedOffDead);
            ResolveProperty(obj, "CreateVampire", g_createVampire);
            ResolveProperty(obj, "VampireRank", g_vampireRank);
            ResolveProperty(obj, "VampireEngorge", g_engorge);
            ResolveProperty(obj, "VampireEngorgeAmount", g_engorgeAmount);
            ResolveProperty(obj, "VampireExtractBlood", g_extractBloodToggle);
            ResolveProperty(obj, "VampireNecksBittenDiscovered", g_necksBittenDiscovered);
            ResolveProperty(obj, "VampireRankProgression", g_rankProgression);
            ResolveProperty(obj, "VampireStatusMessages", g_statusMessages);
            ResolveProperty(obj, "BVSpecialVictimFeeding", g_specialVictimFeeding);
            ResolveProperty(obj, "VampireLastTimeFed", g_lastTimeFed);
            ResolveProperty(obj, "GameDaysPassed", g_gameDaysPassed);
            ResolveProperty(obj, "BVMCMSkillPointsTotal", g_skillPointsTotal);
            ResolveProperty(obj, "BVMCMSkillPointsAvailable", g_skillPointsAvailable);
            ResolveProperty(obj, "BVMCMGiveAllSkillPointsGlobal", g_giveAllSkillPoints);
            ResolveProperty(obj, "VampireNeckMarks", g_neckMarksToggle);
            ResolveProperty(obj, "TargetAlreadyDeadGlobal", g_targetAlreadyDeadGlobal);

            ResolveProperty(obj, "VampirePCFamily", g_vampirePCFamily);
            ResolveProperty(obj, "Vampire", g_vampireKeyword);
            ResolveProperty(obj, "BetterVampiresMenuOptionsSpell", g_menuOptionsSpell);
            ResolveProperty(obj, "VampireVictimDamage2", g_victimDamageSpell);
            ResolveProperty(obj, "BleedingSpell", g_bleedingSpell);
            ResolveProperty(obj, "VampireExtractBloodPotions", g_extractBloodPerk);
            ResolveProperty(obj, "NeckMarksRight", g_neckMarksShader);
            ResolveProperty(obj, "BVPowerfulFeedingVictims", g_powerfulVictims);

            // Papyrus "Sound" properties hold a SOUN record; unwrap to its descriptor for PlaySound
            RE::TESForm* soundForm = nullptr;
            ResolveProperty(obj, "MAGVampireTransform01", soundForm);
            if (soundForm) {
                g_feedSound = soundForm->As<RE::BGSSoundDescriptorForm>();
                if (!g_feedSound) {
                    if (auto* soun = soundForm->As<RE::TESSound>()) g_feedSound = soun->descriptor;
                }
            }
            if (g_feedSound) {
                SKSE::log::debug("  BV feed sound descriptor: 0x{:08X}", g_feedSound->GetFormID());
            } else {
                SKSE::log::debug("  BV feed sound descriptor: missing");
            }

            for (size_t i = 0; i < std::size(kCityLocationProps); ++i) {
                ResolveProperty(obj, kCityLocationProps[i], g_cityLocations[i]);
            }
            for (size_t i = 0; i < std::size(kTownLocationProps); ++i) {
                ResolveProperty(obj, kTownLocationProps[i], g_townLocations[i]);
            }

            // Without BV's core flag the deep path can't work: either the save's script
            // instance predates BV (properties None until the quest is reset) or a foreign
            // PlayerVampireQuestScript.pex is bound. Retry next feed; Papyrus handles this one.
            if (!g_usingBVScripts) {
                SKSE::log::warn("BetterVampiresIntegration: core BV properties unavailable ({} unresolved) - "
                    "save not initialized with Better Vampires, or another mod overrides PlayerVampireQuestScript.pex. "
                    "Falling back to Papyrus (enable debug logging for the per-property list)", g_unresolvedProps);
                return false;
            }

            g_propsResolved = true;
            if (g_unresolvedProps == 0) {
                SKSE::log::info("BetterVampiresIntegration: all script properties resolved");
            } else {
                SKSE::log::warn("BetterVampiresIntegration: {} script properties missing - enable debug logging for the per-property list", g_unresolvedProps);
            }
            return true;
        }

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
            RE::BSFixedString stat("Necks Bitten");
            vm->DispatchStaticCall("Game", "IncrementStat", RE::MakeFunctionArguments(std::move(stat)), callback);
        }

        void TriggerScreenBlood(int count) {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return;

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new VampireIntegrationUtils::EmptyCallback());
            std::int32_t c = count;
            vm->DispatchStaticCall("Game", "TriggerScreenBlood", RE::MakeFunctionArguments(std::move(c)), callback);
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

        // Papyrus IsInLocation semantics: current location or any parent matches
        bool PlayerIsInAnyLocation(RE::PlayerCharacter* player, RE::BGSLocation* const* locations, size_t count) {
            for (auto* current = player->GetCurrentLocation(); current; current = current->parentLoc) {
                for (size_t i = 0; i < count; ++i) {
                    if (locations[i] && current == locations[i]) return true;
                }
            }
            return false;
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

        bool hasBetterVampires = dataHandler->LookupModByName("Better Vampires.esp") != nullptr;
        if (!hasBetterVampires) {
            SKSE::log::info("BetterVampiresIntegration: Better Vampires not installed");
            g_available = false;
            return false;
        }

        g_playerVampireQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("PlayerVampireQuest");

        g_available = g_playerVampireQuest != nullptr;
        if (!g_available) {
            SKSE::log::warn("BetterVampiresIntegration: PlayerVampireQuest MISSING");
            return false;
        }

        SKSE::log::debug("BetterVampiresIntegration: Initialized (script properties resolve on first feed)");
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
            SKSE::log::error("BetterVampiresIntegration::ProcessFeed: target is null");
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("BetterVampiresIntegration::ProcessFeed: player is null");
            return false;
        }

        // Resolve before any mutation so a failure here can still fall back to Papyrus
        if (!ResolveScriptProperties()) {
            SKSE::log::warn("BetterVampiresIntegration::ProcessFeed: properties unresolved - aborting");
            return false;
        }

        SKSE::log::info("BetterVampiresIntegration::ProcessFeed: target={}, lethal={}, combat={}, sleeping={}",
            context.target->GetName(), context.isLethal, context.isCombatFeed, context.isSleeping);

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
            // === STEP 14: Red screen crossfade - SKIP ===

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

        // === STEP 29: Restart the hunger tick from a full stomach ===
        CallPapyrusMethod(g_playerVampireQuest, kScriptName, "UnregisterForUpdateGameTime");
        CallBVMethod("RegisterForUpdateGameTime", 1.0f);

        SKSE::log::info("BetterVampiresIntegration::ProcessFeed: Complete");
        return true;
    }
}
