#include "utils/IdleParser.h"
#include "utils/AnimUtil.h"
#include <unordered_map>
#include <algorithm>
#include <mutex>

namespace IdleParser {
    // Cache of parent->children relationships (built once on first use)
    static std::unordered_map<RE::FormID, std::vector<RE::TESIdleForm*>> g_idleChildrenCache;
    static bool g_idleCacheBuilt = false;

    // Build the idle children cache by scanning all idles and checking their parentIdle
    static void BuildIdleChildrenCache() {
        if (g_idleCacheBuilt) return;

        SKSE::log::info("[IdleParser] Building idle children cache from parentIdle relationships...");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::error("[IdleParser] TESDataHandler not available for cache building");
            return;
        }

        // Scan all idle forms in game
        int totalIdles = 0;
        int idlesWithParent = 0;

        const auto& idleArray = dataHandler->GetFormArray<RE::TESIdleForm>();
        SKSE::log::info("[IdleParser] Form array contains {} TESIdleForm entries", idleArray.size());

        for (auto* idle : idleArray) {
            if (!idle) continue;
            totalIdles++;

            auto* parent = idle->parentIdle;
            if (parent) {
                idlesWithParent++;
                g_idleChildrenCache[parent->GetFormID()].push_back(idle);
            }
        }

        SKSE::log::info("[IdleParser] Cache built: {} total idles, {} have parents, {} unique parents",
            totalIdles, idlesWithParent, g_idleChildrenCache.size());

        g_idleCacheBuilt = true;
    }

    // Build context from actors - pre-computes all derived state for condition evaluation
    IdleSelectionContext BuildIdleContext(RE::Actor* subject, RE::Actor* target) {
        IdleSelectionContext ctx;
        ctx.subject = subject;
        ctx.target = target;

        if (!subject) return ctx;

        // Combat and movement state
        ctx.isInCombat = subject->IsInCombat();
        ctx.isSneaking = subject->IsSneaking();

        // Weapon state
        auto* actorState = subject->AsActorState();
        if (actorState) {
            ctx.hasWeaponDrawn = actorState->IsWeaponDrawn();
        }

        // Get equipped weapon type
        ctx.weaponType = GetEquippedWeaponType(subject, true);
        ctx.hasTwoHandedWeapon = IsTwoHandedWeapon(ctx.weaponType);
        ctx.hasBow = (ctx.weaponType == RE::WEAPON_TYPE::kBow || ctx.weaponType == RE::WEAPON_TYPE::kCrossbow);

        // Check for shield in left hand
        auto* leftEquip = subject->GetEquippedObject(true);  // true = left hand
        if (leftEquip) {
            ctx.hasShield = leftEquip->IsArmor();
        }

        // Check for magic
        auto* rightSpell = subject->GetActorRuntimeData().selectedSpells[RE::Actor::SlotTypes::kRightHand];
        auto* leftSpell = subject->GetActorRuntimeData().selectedSpells[RE::Actor::SlotTypes::kLeftHand];
        ctx.hasMagic = (rightSpell != nullptr || leftSpell != nullptr);

        // Target-relative calculations
        if (target) {
            ctx.angleToTarget = AnimUtil::GetAngleBetween(subject, target);
            ctx.distanceToTarget = subject->GetPosition().GetDistance(target->GetPosition());
            ctx.isBehindTarget = AnimUtil::GetClosestDirection(target, subject);
        }

        return ctx;
    }

    // Retrieve all child idles from a parent idle form
    // Uses parentIdle relationships (like xEdit shows) rather than childIdles array
    std::vector<RE::TESIdleForm*> GetChildIdles(RE::TESIdleForm* parentIdle) {
        std::vector<RE::TESIdleForm*> children;

        if (!parentIdle) return children;

        const char* parentEditorID = parentIdle->GetFormEditorID();
        std::string parentName = parentEditorID ? parentEditorID : "<unnamed>";

        // The childIdles array contains ALL descendants, not just immediate children
        // We need to filter to only include idles whose parentIdle == this idle
        auto* childArray = parentIdle->childIdles;
        if (childArray && childArray->size() > 0) {
            RE::FormID parentFormID = parentIdle->GetFormID();

            // DEBUG: Log what we find for specific idles
            bool isDebugIdle = (parentName.find("DragonRoot") != std::string::npos ||
                               parentName.find("1HMDragon") != std::string::npos);

            if (isDebugIdle) {
                SKSE::log::info("[DEBUG] GetChildIdles for '{}' (FormID: {:08X}), childArray size: {}",
                    parentName, parentFormID, childArray->size());
            }

            for (std::uint32_t i = 0; i < childArray->size(); ++i) {
                auto* form = (*childArray)[i];
                if (auto* childIdle = form ? form->As<RE::TESIdleForm>() : nullptr) {
                    const char* childEditorID = childIdle->GetFormEditorID();
                    std::string childName = childEditorID ? childEditorID : "<unnamed>";

                    RE::FormID childParentFormID = childIdle->parentIdle ? childIdle->parentIdle->GetFormID() : 0;
                    const char* childParentEditorID = childIdle->parentIdle ? childIdle->parentIdle->GetFormEditorID() : nullptr;
                    std::string childParentName = childParentEditorID ? childParentEditorID : "<none>";

                    if (isDebugIdle) {
                        SKSE::log::info("[DEBUG]   Child[{}]: '{}' -> parentIdle='{}' ({:08X}) {}",
                            i, childName, childParentName, childParentFormID,
                            childParentFormID == parentFormID ? "MATCH" : "NO MATCH");
                    }

                    // Only include if this idle's parentIdle points back to our parent
                    if (childIdle->parentIdle && childIdle->parentIdle->GetFormID() == parentFormID) {
                        children.push_back(childIdle);
                    }
                }
            }
            return children;
        }

        // Fallback: Use our parentIdle-based cache
        BuildIdleChildrenCache();

        auto it = g_idleChildrenCache.find(parentIdle->GetFormID());
        if (it != g_idleChildrenCache.end()) {
            children = it->second;
        }

        return children;
    }

    // Build a graph of idles starting from a root idle (recursive)
    IdleNode BuildIdleGraph(RE::TESIdleForm* rootIdle, int maxDepth) {
        IdleNode node;

        if (!rootIdle || maxDepth <= 0) return node;

        node.idle = rootIdle;
        node.editorID = rootIdle->GetFormEditorID() ? rootIdle->GetFormEditorID() : "";
        node.hasConditions = (rootIdle->conditions.head != nullptr);

        SKSE::log::debug("[IdleParser] Building node: '{}' (hasConditions={}, depth={})",
            node.editorID, node.hasConditions, maxDepth);

        // Recursively build children
        auto childIdles = GetChildIdles(rootIdle);
        for (auto* childIdle : childIdles) {
            auto childNode = BuildIdleGraph(childIdle, maxDepth - 1);
            if (childNode.idle) {
                node.children.push_back(std::move(childNode));
            }
        }

        return node;
    }

    // Build graph from EditorID
    IdleNode BuildIdleGraphFromEditorID(const char* rootEditorID, int maxDepth) {
        if (!rootEditorID) return {};

        auto* rootIdle = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(rootEditorID);
        if (!rootIdle) {
            SKSE::log::warn("[IdleParser] Root idle '{}' not found", rootEditorID);
            return {};
        }

        return BuildIdleGraph(rootIdle, maxDepth);
    }

    // Evaluate if an idle's conditions pass for the given context
    bool EvaluateIdleConditions(RE::TESIdleForm* idle, const IdleSelectionContext& context) {
        if (!idle) return false;

        // No conditions = always passes
        if (!idle->conditions.head) {
            SKSE::log::debug("[IdleParser] EvaluateConditions: '{}' has no conditions (auto-pass)",
                idle->GetFormEditorID() ? idle->GetFormEditorID() : "unknown");
            return true;
        }

        if (!context.subject) {
            SKSE::log::warn("[IdleParser] EvaluateConditions: no subject in context");
            return false;
        }

        // Use vanilla condition evaluation
        // TESCondition::operator() takes (actionRef, targetRef)
        auto* subjectRef = context.subject->As<RE::TESObjectREFR>();
        auto* targetRef = context.target ? context.target->As<RE::TESObjectREFR>() : nullptr;

        bool result = idle->conditions(subjectRef, targetRef);

        SKSE::log::debug("[IdleParser] EvaluateConditions: '{}' = {}",
            idle->GetFormEditorID() ? idle->GetFormEditorID() : "unknown",
            result ? "PASS" : "FAIL");

        return result;
    }

    // Internal helper for recursive selection (with verbose logging for debugging)
    // Returns the depth of the deepest matching node found in this branch
    // globalBestDepth is passed by reference to track the best depth across ALL branches
    static int SelectIdleRecursive(const IdleNode& node, const IdleSelectionContext& context,
                                    IdleSelectionResult& result, std::vector<std::string>& currentPath,
                                    int& globalBestDepth, bool verbose = false) {
        if (!node.idle) return 0;

        // Evaluate this node's conditions
        if (!EvaluateIdleConditions(node.idle, context)) {
            if (verbose) {
                SKSE::log::info("[IdleSelect] REJECTED: '{}' - conditions failed", node.editorID);
            }
            return 0;  // Conditions failed, don't continue down this branch
        }

        // Conditions passed - add to path
        currentPath.push_back(node.editorID);
        int currentDepth = static_cast<int>(currentPath.size());

        if (verbose) {
            SKSE::log::info("[IdleSelect] PASSED: '{}' (depth {})", node.editorID, currentDepth);
        }

        // Track deepest match in this branch
        int deepestInBranch = currentDepth;

        // Try to find an even more specific child first
        for (const auto& child : node.children) {
            int childDepth = SelectIdleRecursive(child, context, result, currentPath, globalBestDepth, verbose);
            if (childDepth > deepestInBranch) {
                deepestInBranch = childDepth;
            }
        }

        // Only update result if this is a leaf node (no valid children passed)
        // AND it's deeper than the global best we've found across ALL branches
        if (deepestInBranch == currentDepth && currentDepth > globalBestDepth) {
            // This is a leaf node (no valid children) and it's deeper than any previous best
            result.selectedIdle = node.idle;
            result.editorID = node.editorID;
            result.success = true;
            result.selectionPath = currentPath;
            globalBestDepth = currentDepth;  // Update global best

            if (verbose) {
                SKSE::log::info("[IdleSelect] CANDIDATE: '{}' at depth {} (new global best)", node.editorID, currentDepth);
            }
        }

        currentPath.pop_back();
        return deepestInBranch;
    }

    // Select the best idle from a graph based on conditions
    IdleSelectionResult SelectIdleFromGraph(const IdleNode& root, const IdleSelectionContext& context, bool verbose) {
        IdleSelectionResult result;
        result.success = false;

        if (!root.idle) {
            result.failureReason = "Invalid root node";
            return result;
        }

        std::vector<std::string> currentPath;
        int globalBestDepth = 0;
        SelectIdleRecursive(root, context, result, currentPath, globalBestDepth, verbose);

        if (result.success) {
            SKSE::log::info("[IdleParser] Selected idle: '{}' via path: {}",
                result.editorID,
                [&]() {
                    std::string path;
                    for (size_t i = 0; i < result.selectionPath.size(); ++i) {
                        if (i > 0) path += " -> ";
                        path += result.selectionPath[i];
                    }
                    return path;
                }());
        } else {
            result.failureReason = "No idle matched conditions";
            SKSE::log::warn("[IdleParser] No idle selected from graph rooted at '{}'", root.editorID);
        }

        return result;
    }

    // Convenience: Build graph and select in one call
    IdleSelectionResult SelectIdleFromRoot(RE::TESIdleForm* rootIdle, const IdleSelectionContext& context) {
        auto graph = BuildIdleGraph(rootIdle);
        return SelectIdleFromGraph(graph, context);
    }

    IdleSelectionResult SelectIdleFromRootEditorID(const char* rootEditorID, const IdleSelectionContext& context) {
        auto graph = BuildIdleGraphFromEditorID(rootEditorID);
        return SelectIdleFromGraph(graph, context);
    }

    // =====================================================================
    // Weapon Type Utilities Implementation
    // =====================================================================

    RE::WEAPON_TYPE GetEquippedWeaponType(RE::Actor* actor, bool rightHand) {
        if (!actor) return RE::WEAPON_TYPE::kHandToHandMelee;

        auto* equipped = actor->GetEquippedObject(!rightHand);  // GetEquippedObject: true = left, false = right
        if (!equipped) return RE::WEAPON_TYPE::kHandToHandMelee;

        auto* weapon = equipped->As<RE::TESObjectWEAP>();
        if (!weapon) return RE::WEAPON_TYPE::kHandToHandMelee;

        return weapon->GetWeaponType();
    }

    bool IsOneHandedWeapon(RE::WEAPON_TYPE type) {
        switch (type) {
            case RE::WEAPON_TYPE::kOneHandSword:
            case RE::WEAPON_TYPE::kOneHandDagger:
            case RE::WEAPON_TYPE::kOneHandAxe:
            case RE::WEAPON_TYPE::kOneHandMace:
                return true;
            default:
                return false;
        }
    }

    bool IsTwoHandedWeapon(RE::WEAPON_TYPE type) {
        switch (type) {
            case RE::WEAPON_TYPE::kTwoHandSword:
            case RE::WEAPON_TYPE::kTwoHandAxe:
                return true;
            default:
                return false;
        }
    }

    bool IsRangedWeapon(RE::WEAPON_TYPE type) {
        switch (type) {
            case RE::WEAPON_TYPE::kBow:
            case RE::WEAPON_TYPE::kCrossbow:
            case RE::WEAPON_TYPE::kStaff:
                return true;
            default:
                return false;
        }
    }

    float GetAttackAngle(RE::Actor* attacker, RE::Actor* target) {
        if (!attacker || !target) return 0.0f;

        // Get the angle from attacker to target
        float angleToTarget = AnimUtil::GetAngleBetween(attacker, target);

        // Get attacker's facing direction
        float attackerHeading = attacker->GetAngleZ();

        // Calculate the attack angle (difference between facing and target direction)
        float attackAngle = AnimUtil::normalizeAngle(angleToTarget - attackerHeading);

        return attackAngle;
    }

    // =====================================================================
    // Debug / Logging Utilities Implementation
    // =====================================================================

    // Helper to get condition function name
    const char* GetConditionFunctionName(int funcID) {
        switch (funcID) {
            case 1: return "GetDistance";
            case 6: return "GetActorValue";
            case 8: return "GetDeadCount";
            case 14: return "GetCurrentTime";
            case 18: return "GetClothingValue";
            case 24: return "GetGlobalValue";
            case 25: return "IsMoving";
            case 26: return "GetSecondsPassed";
            case 27: return "GetCurrentProcedure";
            case 32: return "GetAttacked";
            case 35: return "GetWeaponAnimType";
            case 36: return "IsWeaponSkillType";
            case 38: return "IsInInterior";
            case 39: return "IsActorDetected";
            case 41: return "GetDetected";
            case 42: return "GetDead";
            case 43: return "GetItemCount";
            case 44: return "GetGold";
            case 45: return "GetSleeping";
            case 46: return "GetTalkedToPC";
            case 47: return "GetScriptVariable";
            case 53: return "GetAIScriptTarget";
            case 56: return "GetIsRace";
            case 58: return "GetIsClass";
            case 59: return "GetSex";
            case 60: return "GetInSameCell";
            case 61: return "GetIsReference";
            case 62: return "GetIsUsedItem";
            case 67: return "GetIsPlayableRace";
            case 68: return "GetOffersServices";
            case 69: return "GetDisabled";
            case 76: return "GetIsClassDefault";
            case 77: return "GetGhostState";
            case 79: return "GetStageID";
            case 80: return "GetStage";
            case 109: return "GetWalkSpeed";
            case 110: return "GetRunSpeed";
            case 117: return "GetWeaponSpeedMult";
            case 118: return "GetAIState";
            case 122: return "IsAttacking";
            case 123: return "GetPowerAttacking";
            case 125: return "GetPercentChance";
            case 127: return "IsBlocking";
            case 128: return "GetThreatLevel";
            case 129: return "GetSoundLevel";
            case 136: return "IsHostileToActor";
            case 141: return "GetInCell";
            case 149: return "GetArmorRating";
            case 170: return "GetArmorValue";
            case 180: return "GetBaseActorValue";
            case 182: return "GetActorValuePercent";
            case 193: return "GetLevel";
            case 203: return "GetEquipped";
            case 214: return "GetEquippedItemType";
            case 223: return "GetCurrentAIProcedure";
            case 226: return "GetTrespassWarningLevel";
            case 227: return "IsTresspassing";
            case 228: return "IsInMyOwnedCell";
            case 230: return "GetWantBlocking";
            case 237: return "IsActionRef";
            case 242: return "IsCombatTarget";
            case 244: return "SameFaction";
            case 245: return "SameRace";
            case 246: return "SameSex";
            case 254: return "GetInZone";
            case 259: return "GetFactionRank";
            case 261: return "HasFaction";
            case 263: return "GetRelationshipRank";
            case 264: return "GetIsKeyword";
            case 277: return "GetShouldAttack";
            case 280: return "GetInCombat";
            case 282: return "IsEssential";
            case 285: return "GetActorInCombatState";
            case 286: return "GetMovementSpeed";
            case 288: return "HasSpell";
            case 289: return "HasKeyword";
            case 303: return "IsSneaking";
            case 305: return "GetActorAggroRadiusViolated";
            case 306: return "GetCrime";
            case 310: return "GetAttackState";
            case 359: return "IsInInterior";
            case 362: return "HasPerk";
            case 367: return "GetCombatState";
            case 368: return "GetWeaponType";
            case 370: return "IsFleeing";
            case 398: return "GetShoutVariation";
            case 403: return "HasShout";
            case 406: return "HasFamilyRelationship";
            case 408: return "GetActorGhost";
            case 410: return "IsPlayerInJail";
            case 414: return "GetLastHitCritical";
            case 426: return "GetInWorldspace";
            case 432: return "GetEventData";
            case 437: return "IsInKillMove";
            case 444: return "GetActorCrimePlayerEnemy";
            case 448: return "HasKeyword";
            case 454: return "GetIsEditorLocAlias";
            case 459: return "GetShouldHelp";
            case 463: return "GetGraphVariable";
            case 473: return "EPModSkillUsage_AdvanceObjectHasKeyword";
            case 477: return "EPMagicEffect_IsDetectedTarget";
            case 487: return "GetEquippedShout";
            case 495: return "WornHasKeyword";
            case 497: return "GetActorStance";
            case 503: return "CanProduceForWorkshop";
            case 513: return "GetDayOfWeek";
            case 515: return "GetInCurrentLoc";
            case 518: return "GetQuestObjective";
            case 524: return "GetIsVoiceType";
            case 528: return "GetInCurrentLocAlias";
            case 533: return "GetDialogueConditionData";
            case 547: return "GetCastSound";
            case 550: return "GetWingState";
            case 554: return "GetActorUsingKeyword";
            case 555: return "GetIsActor";
            case 560: return "IsCarryable";
            case 561: return "GetConcussed";
            case 573: return "GetActorRefTypeDeadCount";
            case 576: return "GetDestructionStage";
            case 589: return "GetXPForNextLevel";
            case 590: return "GetHasBeenEaten";
            case 591: return "GetRelationshipRank";
            default: return nullptr;
        }
    }

    // Log the idle graph structure to SKSE log
    // Prints all nodes with children and their conditions
    void LogIdleGraph(const IdleNode& node, int indent) {
        if (!node.idle) return;

        bool hasChildren = !node.children.empty();

        // Only print nodes that have children (branch nodes)
        if (!hasChildren) {
            return;
        }

        std::string indentStr(indent * 2, ' ');

        SKSE::log::info("{}[IDLE] '{}' (FormID: {:08X}) Children: {}",
            indentStr,
            node.editorID.empty() ? "<unnamed>" : node.editorID,
            node.idle->GetFormID(),
            node.children.size());

        // Print all conditions for this node
        if (node.hasConditions) {
            auto* condItem = node.idle->conditions.head;
            int condIndex = 0;
            while (condItem) {
                auto funcID = condItem->data.functionData.function.underlying();
                auto opCode = static_cast<int>(condItem->data.flags.opCode);
                float compValue = condItem->data.comparisonValue.f;
                bool isOR = condItem->data.flags.isOR;

                // Get "Run On" target from object field
                auto runOn = condItem->data.object.get();
                const char* runOnStr = "Subject";
                switch (runOn) {
                    case RE::CONDITIONITEMOBJECT::kSelf: runOnStr = "Subject"; break;
                    case RE::CONDITIONITEMOBJECT::kTarget: runOnStr = "Target"; break;
                    case RE::CONDITIONITEMOBJECT::kRef: runOnStr = "Reference"; break;
                    case RE::CONDITIONITEMOBJECT::kCombatTarget: runOnStr = "CombatTarget"; break;
                    case RE::CONDITIONITEMOBJECT::kLinkedRef: runOnStr = "LinkedRef"; break;
                    case RE::CONDITIONITEMOBJECT::kQuestAlias: runOnStr = "QuestAlias"; break;
                    case RE::CONDITIONITEMOBJECT::kPackData: runOnStr = "PackData"; break;
                    case RE::CONDITIONITEMOBJECT::kEventData: runOnStr = "EventData"; break;
                    default: runOnStr = "Unknown"; break;
                }

                // Get function name from enum
                auto funcEnum = condItem->data.functionData.function.get();
                const char* funcName = GetConditionFunctionName(funcID);
                std::string funcStr = funcName ? funcName : fmt::format("Func_{}", funcID);

                // Try to get parameters - could be forms or integer values
                std::string paramStr;
                for (int paramIdx = 0; paramIdx < 2; ++paramIdx) {
                    void* rawParam = condItem->data.functionData.params[paramIdx];
                    if (!rawParam) continue;

                    auto ptrVal = reinterpret_cast<uintptr_t>(rawParam);

                    // Check if it looks like a valid heap pointer (not a small integer value)
                    if (ptrVal > 0x10000) {
                        // Try to interpret as TESForm
                        auto* formParam = static_cast<RE::TESForm*>(rawParam);
                        // Use LookupByID to validate the form exists in game data
                        if (formParam && RE::TESForm::LookupByID(formParam->GetFormID()) == formParam) {
                            const char* editorID = formParam->GetFormEditorID();
                            if (!paramStr.empty()) paramStr += ", ";
                            if (editorID && editorID[0] != '\0') {
                                paramStr += editorID;
                            } else {
                                paramStr += fmt::format("{:08X}", formParam->GetFormID());
                            }
                        }
                    } else if (ptrVal > 0 && ptrVal <= 0x10000) {
                        // Small integer value - likely an enum or index
                        if (!paramStr.empty()) paramStr += ", ";
                        paramStr += fmt::format("{}", ptrVal);
                    }
                }

                // Get comparison operator string
                const char* opStr = "??";
                switch (opCode) {
                    case 0: opStr = "=="; break;
                    case 1: opStr = "!="; break;
                    case 2: opStr = ">"; break;
                    case 3: opStr = ">="; break;
                    case 4: opStr = "<"; break;
                    case 5: opStr = "<="; break;
                }

                if (!paramStr.empty()) {
                    SKSE::log::info("{}  [{}] {}({}) {} {:.0f} [{}] {}",
                        indentStr, condIndex, funcStr, paramStr, opStr, compValue, runOnStr,
                        isOR ? "OR" : "AND");
                } else {
                    SKSE::log::info("{}  [{}] {} {} {:.0f} [{}] {}",
                        indentStr, condIndex, funcStr, opStr, compValue, runOnStr,
                        isOR ? "OR" : "AND");
                }

                condItem = condItem->next;
                condIndex++;
            }
        }

        // List child names for quick reference
        std::string childNames;
        for (size_t i = 0; i < node.children.size(); ++i) {
            if (i > 0) childNames += ", ";
            childNames += node.children[i].editorID.empty() ? "<unnamed>" : node.children[i].editorID;
        }
        SKSE::log::info("{}  -> Children: [{}]", indentStr, childNames);

        // Recursively log children
        for (const auto& child : node.children) {
            LogIdleGraph(child, indent + 1);
        }
    }

    // Log all idles in the game matching a prefix
    void LogIdlesByPrefix(const char* prefix) {
        if (!prefix) return;

        SKSE::log::info("=== Searching for idles with prefix '{}' ===", prefix);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::error("TESDataHandler not available");
            return;
        }

        int count = 0;
        std::string prefixLower(prefix);
        std::transform(prefixLower.begin(), prefixLower.end(), prefixLower.begin(), ::tolower);

        // Iterate all TESIdleForm records
        for (auto* form : dataHandler->GetFormArray<RE::TESIdleForm>()) {
            if (!form) continue;

            const char* editorID = form->GetFormEditorID();
            if (!editorID) continue;

            std::string editorIDLower(editorID);
            std::transform(editorIDLower.begin(), editorIDLower.end(), editorIDLower.begin(), ::tolower);

            if (editorIDLower.find(prefixLower) != std::string::npos) {
                bool hasConditions = (form->conditions.head != nullptr);
                bool hasParent = (form->parentIdle != nullptr);
                bool hasChildren = (form->childIdles && form->childIdles->size() > 0);

                SKSE::log::info("[IDLE] '{}' (FormID: {:08X}) - Parent:{} Children:{} Conditions:{}",
                    editorID,
                    form->GetFormID(),
                    hasParent ? "Yes" : "No",
                    hasChildren ? std::to_string(form->childIdles->size()) : "0",
                    hasConditions ? "Yes" : "No");

                count++;
            }
        }

        SKSE::log::info("=== Found {} idles matching '{}' ===", count, prefix);
    }

    // Helper to count total nodes in graph
    static int CountIdleNodes(const IdleNode& node) {
        int count = node.idle ? 1 : 0;
        for (const auto& child : node.children) {
            count += CountIdleNodes(child);
        }
        return count;
    }

    // Dump complete idle hierarchy starting from an EditorID
    void DumpIdleHierarchy(const char* rootEditorID, int maxDepth) {
        if (!rootEditorID) return;

        SKSE::log::info("======================================================");
        SKSE::log::info("IDLE HIERARCHY DUMP: '{}' (maxDepth={})", rootEditorID, maxDepth);
        SKSE::log::info("======================================================");

        auto graph = BuildIdleGraphFromEditorID(rootEditorID, maxDepth);
        if (!graph.idle) {
            SKSE::log::error("Could not find or build graph for '{}'", rootEditorID);
            return;
        }

        int totalNodes = CountIdleNodes(graph);
        SKSE::log::info("Total nodes in graph: {}", totalNodes);

        LogIdleGraph(graph);

        SKSE::log::info("======================================================");
        SKSE::log::info("END HIERARCHY DUMP ({} nodes)", totalNodes);
        SKSE::log::info("======================================================");
    }

    // Debug: Find and log the kill move that would match for player vs target
    void DebugFindKillMove(RE::Actor* player, RE::Actor* target) {
        if (!player || !target) {
            SKSE::log::warn("[DebugKillMove] Invalid player or target");
            return;
        }

        SKSE::log::info("=== DEBUG KILL MOVE SELECTION ===");
        SKSE::log::info("Player: {} (FormID: {:08X})", player->GetName(), player->GetFormID());
        SKSE::log::info("Target: {} (FormID: {:08X})", target->GetName(), target->GetFormID());

        // Build context from player and target
        auto context = BuildIdleContext(player, target);

        // Log context info
        SKSE::log::info("Context:");
        SKSE::log::info("  - WeaponType: {}", static_cast<int>(context.weaponType));
        SKSE::log::info("  - WeaponDrawn: {}", context.hasWeaponDrawn);
        SKSE::log::info("  - TwoHanded: {}", context.hasTwoHandedWeapon);
        SKSE::log::info("  - Shield: {}", context.hasShield);
        SKSE::log::info("  - Bow: {}", context.hasBow);
        SKSE::log::info("  - Magic: {}", context.hasMagic);
        SKSE::log::info("  - Sneaking: {}", context.isSneaking);
        SKSE::log::info("  - InCombat: {}", context.isInCombat);
        SKSE::log::info("  - BehindTarget: {}", context.isBehindTarget);
        SKSE::log::info("  - AngleToTarget: {:.2f} rad ({:.1f} deg)",
            context.angleToTarget, AnimUtil::toDegrees(context.angleToTarget));
        SKSE::log::info("  - Distance: {:.1f}", context.distanceToTarget);

        // Check if target has ActorTypeNPC keyword
        auto* npcKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");
        if (npcKeyword && target->HasKeyword(npcKeyword)) {
            SKSE::log::info("  - Target has ActorTypeNPC keyword: YES");
        } else {
            SKSE::log::info("  - Target has ActorTypeNPC keyword: NO");
        }

        // Try to select from NonMountedCombatRightPower tree (kill moves)
        SKSE::log::info("Evaluating kill move tree 'NonMountedCombatRightPower' (VERBOSE)...");

        // Build graph and select with verbose logging
        auto graph = BuildIdleGraphFromEditorID("NonMountedCombatRightPower", 20);

        // Log immediate children of root to verify kill move branches are present
        SKSE::log::info("Root '{}' has {} immediate children:", graph.editorID, graph.children.size());
        for (const auto& child : graph.children) {
            SKSE::log::info("  - '{}' (hasConditions: {}, children: {})",
                child.editorID, child.hasConditions, child.children.size());
        }

        auto result = SelectIdleFromGraph(graph, context, true);  // verbose = true

        if (result.success) {
            SKSE::log::info("=== SELECTED KILL MOVE: '{}' ===", result.editorID);
            SKSE::log::info("Selection path:");
            for (size_t i = 0; i < result.selectionPath.size(); ++i) {
                SKSE::log::info("  [{}] {}", i, result.selectionPath[i]);
            }
        } else {
            SKSE::log::warn("=== NO KILL MOVE SELECTED ===");
            SKSE::log::warn("Reason: {}", result.failureReason);
        }

        SKSE::log::info("=== END DEBUG KILL MOVE SELECTION ===");
    }
}
