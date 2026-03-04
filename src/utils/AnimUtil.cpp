#include "utils/AnimUtil.h"
#include "feed/TargetState.h"
#include "feed/CustomFeed.h"
#include "Settings.h"
#include "papyrus/PapyrusCall.h"
#include <memory>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <bit>

namespace {
    // Union for condition parameter handling
    union ConditionParam {
        char         c;
        std::int32_t i;
        float        f;
        RE::TESForm* form;
    };

    // Native paired idle function - bypasses some engine checks
    // a5 (true) = Force play - bypass checks that might prevent the idle (like weapon state checks)
    // a6 (false) = Don't reset action state - prevents triggering weapon sheathing/state transitions
    inline bool _playPairedIdle(RE::AIProcess* proc, RE::Actor* attacker, RE::DEFAULT_OBJECT smth,
                                RE::TESIdleForm* idle, bool forcePlay, bool resetActionState, RE::TESObjectREFR* target)
    {
        SKSE::log::info("[AnimUtil::playIdle] using custom playIdle");
        using func_t = decltype(&_playPairedIdle);
        REL::Relocation<func_t> func{RELOCATION_ID(38290, 39256)};
        return func(proc, attacker, smth, idle, forcePlay, resetActionState, target);
    }
}

namespace AnimUtil {
    // Continuous task management
    static std::mutex g_ContinuousTasksMutex;
    static std::unordered_set<std::string> g_ActiveContinuousTasks;

    // Track frame counts per task to throttle updates
    static std::unordered_map<std::string, uint32_t> g_TaskFrameCounters;

    // Update frequency: 1 = every frame, 2 = every other frame, 3 = every 3rd frame, etc.
    static constexpr uint32_t UPDATE_FREQUENCY = 2;  // Update every 2nd frame (~30Hz at 60 FPS)

    // Helper to check if task should continue
    static bool isTaskActive(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(g_ContinuousTasksMutex);
        return g_ActiveContinuousTasks.contains(taskId);
    }

    // Helper to check if this frame should update (throttling)
    static bool shouldUpdateThisFrame(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(g_ContinuousTasksMutex);
        auto& counter = g_TaskFrameCounters[taskId];
        counter++;
        return (counter % UPDATE_FREQUENCY) == 0;
    }
    // Play animation without speed control
    void playAnimation(RE::Actor* actor, const std::string& animation) {
        if (!actor) return;

        auto actorHandle = actor->CreateRefHandle();
        SKSE::GetTaskInterface()->AddTask([actorHandle, animation] {
            auto actorRef = actorHandle.get();
            if (auto* a = actorRef.get()) {
                a->NotifyAnimationGraph(animation);
            }
        });
    }

    // Play animation with speed control
    void playAnimation(RE::Actor* actor, const std::string& animation, float playbackSpeed) {
        if (!actor) return;

        auto actorHandle = actor->CreateRefHandle();
        SKSE::GetTaskInterface()->AddTask([actorHandle, animation, playbackSpeed] {
            auto actorRef = actorHandle.get();
            if (auto* a = actorRef.get()) {
                a->NotifyAnimationGraph(animation);
            }
        });
    }

    // Preprocessing for paired animations - clears stagger/attack/knockdown states
    // Must be called on main thread (via SKSE task)
    void PrepareActorForPairedIdle(RE::Actor* actor) {
        if (!actor) return;

        // Stop any ongoing attack/stagger animations
        actor->NotifyAnimationGraph("attackStop");
        actor->NotifyAnimationGraph("staggerStop");

        // Handle knocked-down state - force actor to normal state if getting up
        auto* actorState = actor->AsActorState();
        if (actorState) {
            auto knockState = actorState->GetKnockState();
            if (knockState == RE::KNOCK_STATE_ENUM::kGetUp ||
                knockState == RE::KNOCK_STATE_ENUM::kQueued) {
                actorState->actorState1.knockState = RE::KNOCK_STATE_ENUM::kNormal;
                actor->NotifyAnimationGraph("GetUpEnd");
                SKSE::log::debug("[AnimUtil::PrepareActorForPairedIdle] Reset knock state for {}",
                    actor->GetName());
            }
        }
    }

    // Play idle animation (thread-safe)
    void playIdle(RE::Actor* actor, RE::TESIdleForm* idle, RE::TESObjectREFR* target) {
        if (!actor || !idle) {
            SKSE::log::warn("[AnimUtil::playIdle] Invalid input: actor={}, idle={}",
                actor ? "valid" : "null", idle ? "valid" : "null");
            return;
        }

        auto actorHandle = actor->CreateRefHandle();
        auto idleFormID = idle->GetFormID();
        auto targetHandle = target ? target->CreateRefHandle() : RE::ObjectRefHandle();
        auto actorName = actor->GetName() ? std::string(actor->GetName()) : std::string("sourceActorNameNone");
        auto targetName = target ? std::string(target->GetName()) : std::string("targetActorNameNone");

        SKSE::log::debug("[AnimUtil::playIdle] Queuing idle {:X} for {} (target: {})",
            idleFormID, actorName, targetName);

        SKSE::GetTaskInterface()->AddTask([actorHandle, idleFormID, targetHandle, actorName, targetName] {
            // 1. Resolve the handle to get NiPointer<TESObjectREFR>
            auto refPtr = actorHandle.get();
            if (!refPtr) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: Actor handle invalid for {}", actorName);
                return;
            }

            // 2. Cast the generic TESObjectREFR to Actor* (critical for accessing currentProcess)
            auto* a = refPtr->As<RE::Actor>();
            if (!a) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: Object is not an Actor for {}", actorName);
                return;
            }

            auto* idle = RE::TESForm::LookupByID<RE::TESIdleForm>(idleFormID);
            if (!idle) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: Idle form {:X} not found for {}", idleFormID, actorName);
                return;
            }

            RE::TESObjectREFR* t = nullptr;
            RE::Actor* targetActor = nullptr;
            if (targetHandle) {
                auto targetRefPtr = targetHandle.get();
                if (!targetRefPtr) {
                    SKSE::log::warn("[AnimUtil::playIdle] Target handle invalid for {}, playing without target", actorName);
                } else {
                    // Target can be TESObjectREFR (for furniture/beds) or Actor, so we use .get()
                    t = targetRefPtr.get();
                    targetActor = t->As<RE::Actor>();
                }
            }

            // Preprocess for paired animations - clear stagger/attack/knockdown states
            if (targetActor) {
                SKSE::log::debug("[AnimUtil::playIdle] Preprocessing actors for paired idle");
                PrepareActorForPairedIdle(a);
                PrepareActorForPairedIdle(targetActor);
            }

            auto* process = a->GetActorRuntimeData().currentProcess;
            if (!process) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: No process for {}", actorName);
                return;
            }

            // Use native paired idle function with:
            // - forcePlay=true: bypass weapon state checks
            // - resetActionState=false: prevent weapon sheathing/state transition
             
            bool success = process->PlayIdle(a, idle, t);
            // bool success = _playPairedIdle(process, a, RE::DEFAULT_OBJECT::kActionIdle, idle, true, false, t);
            if (success) {
                SKSE::log::info("[AnimUtil::playIdle] SUCCESS: Idle {:X} started on {} (target: {})",
                    idleFormID, actorName, targetName);
            } else {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: _playPairedIdle returned false for {} (idle: {:X})",
                    actorName, idleFormID);
            }
        });
    }

    // Set actor rotation (handles player vs NPC differently)
    void setRotation(RE::Actor* actor, float rotation) {
        if (!actor) return;

        auto actorHandle = actor->CreateRefHandle();
        SKSE::GetTaskInterface()->AddTask([actorHandle, rotation] {
            auto actorRef = actorHandle.get();
            if (auto* a = actorRef.get()) {
                if (a->IsPlayerRef()) {
                    a->SetHeading(rotation);
                } else {
                    a->SetAngle(RE::NiPoint3{0.0f, 0.0f, rotation});
                }
            }
        });
    }

    // Set actor position and rotation
    void setPosition(RE::Actor* actor, float x, float y, float z, float rotation) {
        if (!actor) return;

        auto actorHandle = actor->CreateRefHandle();
        SKSE::GetTaskInterface()->AddTask([actorHandle, x, y, z, rotation] {
            auto actorRef = actorHandle.get();
            if (auto* a = actorRef.get()) {
                a->StopMoving(1.0f);
                a->SetPosition(RE::NiPoint3{x, y, z}, false);

                if (a->IsPlayerRef()) {
                    a->SetHeading(rotation);
                } else {
                    a->SetAngle(RE::NiPoint3{0.0f, 0.0f, rotation});
                    a->Update3DPosition(true);
                }
            }
        });
    }

    // Align actor using 2D rotation matrix for proper positioning relative to scene center
    void alignActor(RE::Actor* actor, Position sceneCenter, Alignment alignment, Position additionalOffset) {
        if (!actor) return;
        
        constexpr float PI = 3.14159265358979323846f;
        constexpr float TWO_PI = 2.0f * PI;

        // Get current actor state for logging
        float currentRotation = actor->GetAngleZ();
        auto currentPos = actor->GetPosition();

        // Calculate sine and cosine for rotation matrix
        float sin = std::sin(sceneCenter.r);
        float cos = std::cos(sceneCenter.r);

        // Combine alignment offsets with additional offsets
        float x = alignment.offsetX + additionalOffset.x;
        float y = alignment.offsetY + additionalOffset.y;
        float z = alignment.offsetZ + additionalOffset.z;
        float r = alignment.rotation + additionalOffset.r;

        // Apply 2D rotation matrix transformation
        // This rotates the offset relative to the scene center's rotation
        float finalX = sceneCenter.x + cos * x + sin * y;
        float finalY = sceneCenter.y - sin * x + cos * y;
        float finalZ = sceneCenter.z + z;
        float finalR = sceneCenter.r + toRadians(r);

        // float diff = finalR - currentRotation;
        // while (diff <= -PI) diff += TWO_PI;
        // while (diff > PI) diff -= TWO_PI;
        // float shortestPathTarget = currentRotation + diff;
        // float normalizedLogR = normalizeAngle(finalR);
        // SKSE::log::info("[AnimUtil::alignActor] {} - Pos: ({:.2f}, {:.2f}, {:.2f}), "
        //             "Angle: {:.1f}° -> {:.1f}° (Shortest Target: {:.1f}°)",
        //     actor->GetName(),
        //     currentPos.x, currentPos.y, currentPos.z,
        //     toDegrees(currentRotation), toDegrees(normalizedLogR), toDegrees(shortestPathTarget));

        // setPosition(actor, finalX, finalY, finalZ, shortestPathTarget);

        // Normalize the target angle to prevent full circle rotation
        float normalizedR = normalizeAngle(finalR);
        float rotationDiff = angleDifference(currentRotation, normalizedR);

        SKSE::log::info("[AnimUtil::alignActor] {} - Pos: ({:.2f}, {:.2f}, {:.2f}) -> ({:.2f}, {:.2f}, {:.2f}), "
                       "Angle: {:.1f}° -> {:.1f}° (diff: {:.1f}°)",
            actor->GetName(),
            currentPos.x, currentPos.y, currentPos.z,
            finalX, finalY, finalZ,
            toDegrees(currentRotation), toDegrees(normalizedR), toDegrees(rotationDiff));

        // Set the final position with normalized rotation
        setPosition(actor, finalX, finalY, finalZ, normalizedR);

        // // Temporarily maintain position to prevent actor from moving
        // std::string taskId = "align_" + std::string(actor->GetName()) + "_" + std::to_string(actor->GetFormID());
        // maintainActorPosition(actor, finalX, finalY, finalZ, normalizedR, taskId);
    }

    // Stop animation and reset actor to idle state
    void stopAnimation(RE::Actor* actor) {
        if (!actor) return;

        auto actorHandle = actor->CreateRefHandle();
        bool isPlayer = actor->IsPlayerRef();

        // Handle player separately
        if (isPlayer) {
            SKSE::GetTaskInterface()->AddTask([actorHandle] {
                auto actorRef = actorHandle.get();
                if (auto* a = actorRef.get()) {
                    RE::PlayerCharacter::GetSingleton()->SetAIDriven(false);
                    RE::PlayerControls::GetSingleton()->activateHandler->disabled = false;

                    // Safety: only access graph if 3D is loaded
                    if (a->Is3DLoaded()) {
                        a->SetGraphVariableBool("bHumanoidFootIKDisable", false);
                        a->SetGraphVariableBool("bHeadTrackSpine", true);
                        a->SetGraphVariableBool("bHeadTracking", true);
                        a->SetGraphVariableBool("tdmHeadtrackingBehavior", true);
                        a->NotifyAnimationGraph("IdleForceDefaultState");
                    }
                }
            });
        } else {
            // NPC handling
            SKSE::GetTaskInterface()->AddTask([actorHandle] {
                auto actorRef = actorHandle.get();
                if (auto* a = actorRef.get()) {
                    // Safety: only access graph if 3D is loaded
                    if (!a->Is3DLoaded()) return;

                    a->SetGraphVariableBool("bHumanoidFootIKDisable", false);
                    a->SetGraphVariableBool("bHeadTrackSpine", true);
                    a->SetGraphVariableBool("bHeadTracking", true);
                    a->SetGraphVariableBool("tdmHeadtrackingBehavior", true);

                    // Send multiple reset events to ensure animation stops
                    a->NotifyAnimationGraph("Reset");
                    a->NotifyAnimationGraph("ReturnToDefault");
                    a->NotifyAnimationGraph("FNISDefault");
                    a->NotifyAnimationGraph("IdleReturnToDefault");
                    a->NotifyAnimationGraph("ForceFurnExit");
                    a->NotifyAnimationGraph("ReturnDefaultState");
                }
            });
        }
    }

    // Redraw weapon/magic after animation (restores drawn state)
    void redrawWeapon(RE::Actor* actor) {
        if (!actor) return;

        auto actorHandle = actor->CreateRefHandle();

        SKSE::GetTaskInterface()->AddTask([actorHandle] {
            auto actorRef = actorHandle.get();
            if (auto* a = actorRef.get()) {
                // Use DrawWeaponMagicHands which is the actual function to draw weapons
                a->DrawWeaponMagicHands(true);
                SKSE::log::info("[AnimUtil::redrawWeapon] Called DrawWeaponMagicHands for {}", a->GetName());
            }
        });
    }

    // Set actor restrained state (calls Papyrus native function via VM)
    void setRestrained(RE::Actor* actor, bool restrained) {
        if (!actor) return;

        auto actorHandle = actor->CreateRefHandle();
        SKSE::GetTaskInterface()->AddTask([actorHandle, restrained] {
            auto actorRef = actorHandle.get();
            if (auto* a = actorRef.get()) {
                auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                if (!vm) return;

                auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(RE::Actor::FORMTYPE, a);
                if (handle == vm->GetObjectHandlePolicy()->EmptyHandle()) return;

                bool restrainedArg = restrained;
                auto* args = RE::MakeFunctionArguments(std::move(restrainedArg));
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;

                bool result = vm->DispatchMethodCall(handle, "Actor", "SetRestrained", args, callback);

                // DispatchMethodCall takes ownership only on success; delete on failure
                if (!result) {
                    delete args;
                }
            }
        });
    }

    // Continuous positioning - maintains actor at fixed position until stopped
    void maintainActorPosition(RE::Actor* actor, float x, float y, float z, float rotation, const std::string& taskId) {
        if (!actor) return;

        // Register task as active
        {
            std::lock_guard<std::mutex> lock(g_ContinuousTasksMutex);
            g_ActiveContinuousTasks.insert(taskId);
            g_TaskFrameCounters[taskId] = 0;
        }

        // Create handle for safe async access
        auto actorHandle = actor->CreateRefHandle();

        // Use shared_ptr to allow self-capture in lambda
        auto updateFunc = std::make_shared<std::function<void()>>();
        *updateFunc = [actorHandle, x, y, z, rotation, taskId, updateFunc]() {
            // Check if task was cancelled
            if (!isTaskActive(taskId)) {
                // Clean up frame counter
                std::lock_guard<std::mutex> lock(g_ContinuousTasksMutex);
                g_TaskFrameCounters.erase(taskId);
                return;  // Stop, don't reschedule
            }

            // Always reschedule first to maintain loop, even if we skip this frame
            SKSE::GetTaskInterface()->AddTask(*updateFunc);

            // Throttle: only update every Nth frame
            if (!shouldUpdateThisFrame(taskId)) {
                return;  // Skip this frame, already rescheduled
            }

            // Get actor from handle
            auto actorRef = actorHandle.get();
            if (!actorRef) {
                // Actor invalid, stop task
                std::lock_guard<std::mutex> lock(g_ContinuousTasksMutex);
                g_ActiveContinuousTasks.erase(taskId);
                g_TaskFrameCounters.erase(taskId);
                return;
            }

            auto* a = actorRef.get();
            if (!a || a->IsDead()) {
                // Actor dead, stop task
                std::lock_guard<std::mutex> lock(g_ContinuousTasksMutex);
                g_ActiveContinuousTasks.erase(taskId);
                g_TaskFrameCounters.erase(taskId);
                return;
            }

            // Apply position update (use existing thread-safe setPosition)
            setPosition(a, x, y, z, rotation);
        };

        // Start the update loop
        SKSE::GetTaskInterface()->AddTask(*updateFunc);
    }

    // OSTIM code
    void TranslateTo(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object,
        float afX, float afY, float afZ, float afAngleX, float afAngleY, float afAngleZ, float afSpeed, float afMaxRotationSpeed) {
        using func_t = void(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::TESObjectREFR*, float, float, float, float, float, float, float, float);
        REL::Relocation<func_t> func{RELOCATION_ID(55706, 56237)};
        func(vm, stackID, object, afX, afY, afZ, afAngleX, afAngleY, afAngleZ, afSpeed, afMaxRotationSpeed);
    }

    void StopTranslation(RE::BSScript::IVirtualMachine* vm, RE::VMStackID stackID, RE::TESObjectREFR* object) {
        using func_t = void(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::TESObjectREFR*);
        REL::Relocation<func_t> func{RELOCATION_ID(55712, 56243)};
        func(vm, stackID, object);
    }

    void LockAtPosition(RE::Actor* actor, float x, float y, float z, float rotationRad, bool applyRotation) {
        if (!actor) return;

        auto actorHandle = actor->CreateRefHandle();
        SKSE::GetTaskInterface()->AddTask([actorHandle, x, y, z, rotationRad, applyRotation]() {
            auto actorRef = actorHandle.get();
            if (auto* a = actorRef.get()) {
                StopTranslation(nullptr, 0, a);

                float finalRotationRad = rotationRad;
                if (applyRotation) {
                    // SetHeading doesn't work on NPCs, SetAngle causes stuttering on PC
                    if (a->IsPlayerRef()) {
                        a->SetHeading(rotationRad);
                    } else {
                        a->SetAngle(RE::NiPoint3{0, 0, rotationRad});
                    }
                } else {
                    // Keep actor's current rotation
                    finalRotationRad = a->GetAngleZ();
                }

                // TranslateTo with very fast speed to instantly move and lock position
                float rotationDeg = finalRotationRad * 57.2957795f;  // rad to deg
                TranslateTo(nullptr, 0, a, x, y, z, 0, 0, rotationDeg + 1.0f, 1000000.0f, 0.0001f);
            }
        });
    }

    // Player feed validation (moved from PairedAnimPromptSink)

    // Check if player race supports feeding (Vampire/Werewolf/VampireLord)
    // This is a lightweight check that only verifies race - no hunger/combat checks
    // Use this for early-exit optimizations in periodic checks
    bool IsPlayerFeedingRace() {
        auto* settings = Settings::GetSingleton();

        // Debug override - always return true if ForceVampire enabled
        if (settings->General.ForceVampire) {
            return true;
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        // Check player type
        bool isVampire = TargetState::IsVampire(player);
        bool isWerewolf = TargetState::IsWerewolf(player);
        bool isVampireLord = TargetState::IsVampireLord(player);

        // Check feature flags
        if (isWerewolf && !settings->General.EnableWerewolf) {
            return false;
        }
        if (isVampireLord && !settings->General.EnableVampireLord) {
            return false;
        }

        if (!isVampire && !isWerewolf && !isVampireLord) {
            return false;
        }

        return true;
    }

    bool CanPlayerFeed(bool targetInCombat) {
        // First check if player is the right race
        if (!IsPlayerFeedingRace()) {
            SKSE::log::debug("CanPlayerFeed: false - not a feeding race");
            return false;
        }

        auto* settings = Settings::GetSingleton();
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        // At this point we know player is vampire/werewolf/VL
        // Now check hunger stage requirement (Vampires only, exclude VL/Werewolf)
        bool isVampire = TargetState::IsVampire(player);
        bool isWerewolf = TargetState::IsWerewolf(player);
        bool isVampireLord = TargetState::IsVampireLord(player);

        // Always log vampire stage for debugging (vampires and vampire lords)
        if (isVampire || isVampireLord) {
            int vampireStage = PapyrusCall::GetVampireStage();
            SKSE::log::info("CanPlayerFeed: vampireStage={}, isVampireLord={}", vampireStage, isVampireLord);
        }

        if (isVampire && !isVampireLord && !isWerewolf && settings->General.CheckHungerStage) {
            // Combat targets can bypass hunger check if IgnoreHungerCheck is enabled
            if (targetInCombat && settings->Combat.IgnoreHungerCheck) {
                return true;
            }

            int vampireStage = PapyrusCall::GetVampireStage();
            SKSE::log::debug("CanPlayerFeed: vampireStage={}, minRequired={}", vampireStage, settings->General.MinHungerStage);
            if (vampireStage < settings->General.MinHungerStage) {
                SKSE::log::debug("CanPlayerFeed: false - hunger stage {} < min {}", vampireStage, settings->General.MinHungerStage);
                return false;
            }
        }

        return true;
    }

    // Animation selection and state checking (moved from PairedAnimPromptSink)
    bool IsInPairedAnimation(RE::Actor* actor) {
        if (!actor) return false;

        // Safety: check if actor has valid 3D before accessing animation graph
        // This prevents crashes when NPC is being unloaded or has corrupted animation state
        if (!actor->Is3DLoaded()) return false;

        bool result = false;
        actor->GetGraphVariableBool("bIsSynced", result);
        if (result) return true;
        actor->GetGraphVariableBool("bInKillMove", result);
        if (result) return true;
        return false;
    }

    int DetermineTargetState(RE::Actor* target, bool& outIsInCombat) {
        // Check dead first - dead actors are never in combat
        if (target->IsDead()) {
            outIsInCombat = false;
            return AnimUtil::kDead;
        }
        outIsInCombat = target->IsInCombat();
        if (outIsInCombat) return AnimUtil::kCombat;
        else if (TargetState::IsSleeping(target)) return AnimUtil::kSleeping;
        else if (TargetState::IsSitting(target)) return AnimUtil::kSitting;
        return AnimUtil::kStanding;
    }

    void ApplyHeightAdjustment(RE::Actor* attacker, RE::Actor* target, float minHeightDiff, float maxHeightDiff) {
        auto attackerPos = attacker->GetPosition();
        auto targetPos = target->GetPosition();
        float heightDiff = std::fabs(targetPos.z - attackerPos.z);

        SKSE::log::info("ApplyHeightAdjustment: heightDiff={:.2f}, min={:.2f}, max={:.2f}",
            heightDiff, minHeightDiff, maxHeightDiff);

        if (heightDiff <= minHeightDiff) {
            SKSE::log::info("ApplyHeightAdjustment: SKIPPED - height diff {:.2f} <= min {:.2f}",
                heightDiff, minHeightDiff);
            return;
        }

        if (heightDiff > maxHeightDiff) {
            SKSE::log::warn("ApplyHeightAdjustment: SKIPPED - height diff {:.2f} > max {:.2f}",
                heightDiff, maxHeightDiff);
            return;
        }

        float higherZ = std::max(attackerPos.z, targetPos.z);
        if (attackerPos.z < targetPos.z) {
            SKSE::log::info("ApplyHeightAdjustment: Moving attacker UP from {:.2f} to {:.2f} (diff: {:.2f})",
                attackerPos.z, higherZ, heightDiff);
            attacker->SetPosition(RE::NiPoint3(attackerPos.x, attackerPos.y, higherZ), true);
        } else {
            SKSE::log::info("ApplyHeightAdjustment: Moving target UP from {:.2f} to {:.2f} (diff: {:.2f})",
                targetPos.z, higherZ, heightDiff);
            target->SetPosition(RE::NiPoint3(targetPos.x, targetPos.y, higherZ), true);
        }
    }

    const char* SelectIdleAnimation(int targetState, RE::Actor* target,
                                           const RE::NiPointer<RE::TESObjectREFR>& furnitureRef, bool isBehind,
                                           bool& outIsPairedAnim) {
        outIsPairedAnim = true;
        auto player = RE::PlayerCharacter::GetSingleton();

        // Special handling for Werewolf and Vampire Lord
        if (player) {
            if (TargetState::IsWerewolf(player)) {
                SKSE::log::debug("Player is Werewolf - using werewolf feed");
                // Werewolf feeding usually works on standing targets or forces them
                return Idles::WEREWOLF_STANDING_FRONT;
            }
            if (TargetState::IsVampireLord(player)) {
                SKSE::log::debug("Player is Vampire Lord - using VL feed");
                return isBehind ? Idles::VAMPIRELORD_STANDING_BACK : Idles::VAMPIRELORD_STANDING_FRONT;
            }
        }

        // Dead targets use bedroll animation (corpse on ground)
        if (targetState == AnimUtil::kDead) {
            outIsPairedAnim = false;
            bool isLeft = CustomFeed::IsPlayerOnLeftSide(target);
            SKSE::log::debug("Dead target - using bedroll {} side (solo idle)", isLeft ? "left" : "right");
            return isLeft ? Idles::VAMPIRE_BEDROLL_LEFT : Idles::VAMPIRE_BEDROLL_RIGHT;
        }

        if (targetState == AnimUtil::kSleeping && furnitureRef) {
            outIsPairedAnim = false;
            bool isLeft = CustomFeed::IsPlayerOnLeftSide(target);
            bool isBedroll = CustomFeed::IsBedroll(furnitureRef.get());

            if (isBedroll) {
                SKSE::log::debug("Bedroll {} side (solo idle)", isLeft ? "left" : "right");
                return isLeft ? Idles::VAMPIRE_BEDROLL_LEFT : Idles::VAMPIRE_BEDROLL_RIGHT;
            } else {
                SKSE::log::debug("Bed {} side (solo idle)", isLeft ? "left" : "right");
                return isLeft ? Idles::VAMPIRE_BED_LEFT : Idles::VAMPIRE_BED_RIGHT;
            }
        }

        const char* posStr = isBehind ? "back" : "front";

        if (targetState == AnimUtil::kSitting) {
            SKSE::log::debug("Sitting {} feed", posStr);
            return isBehind ? Idles::VAMPIRE_SITTING_BACK : Idles::VAMPIRE_SITTING_FRONT;
        } else if (targetState == AnimUtil::kCombat) {
            SKSE::log::debug("Combat {} feed (kill move)", posStr);
            return isBehind ? Idles::BACK_SNEAK_KM_A : Idles::FRONT_KM_A;
        } else {
            SKSE::log::debug("Standing {} feed", posStr);
            return isBehind ? Idles::VAMPIRE_STANDING_BACK : Idles::VAMPIRE_STANDING_FRONT;
        }
    }

    // Animation graph variable management (moved from PairedAnimPromptSink)
    void SetFeedGraphVars(RE::Actor* actor, int feedType) {
        if (!actor) return;

        // Safety: check if actor has valid 3D before accessing animation graph
        if (!actor->Is3DLoaded()) {
            SKSE::log::warn("SetFeedGraphVars: {} has no 3D loaded, skipping", actor->GetName());
            return;
        }

        constexpr auto IsSkyPromptFeeding = "IsSkyPromptFeeding";
        constexpr auto SkyPromptFeedType = "SkyPromptFeedType";

        bool success = actor->SetGraphVariableBool(IsSkyPromptFeeding, true);
        if (success) {
            SKSE::log::debug("Set graph var {} = true on {}", IsSkyPromptFeeding, actor->GetName());
        }
        success = actor->SetGraphVariableInt(SkyPromptFeedType, feedType);
        if (success) {
            SKSE::log::debug("Set graph var {} = {} on {}", SkyPromptFeedType, feedType, actor->GetName());
        }
    }

    void ClearFeedGraphVars(RE::Actor* actor) {
        if (!actor) return;

        // Safety: check if actor has valid 3D before accessing animation graph
        if (!actor->Is3DLoaded()) {
            SKSE::log::debug("ClearFeedGraphVars: {} has no 3D loaded, skipping", actor->GetName());
            return;
        }

        constexpr auto IsSkyPromptFeeding = "IsSkyPromptFeeding";
        constexpr auto SkyPromptFeedType = "SkyPromptFeedType";

        actor->SetGraphVariableBool(IsSkyPromptFeeding, false);
        actor->SetGraphVariableInt(SkyPromptFeedType, 0);
        SKSE::log::debug("Cleared feed graph vars on {}", actor->GetName());
    }

    // Calculate angle from one actor to another (in radians)
    float GetAngleBetween(RE::Actor* from, RE::Actor* to) {
        if (!from || !to) return 0.0f;

        auto fromPos = from->GetPosition();
        auto toPos = to->GetPosition();

        float dx = toPos.x - fromPos.x;
        float dy = toPos.y - fromPos.y;

        // atan2 gives angle from 'from' to 'to'
        return std::atan2(dx, dy);
    }

    // Get closest direction (front/back) without rotating target
    // Returns true if back is closer, false if front is closer
    bool GetClosestDirection(RE::Actor* target, RE::Actor* reference) {
        if (!target || !reference) return false;

        float angleToReference = GetAngleBetween(target, reference);
        float currentHeading = target->GetAngleZ();

        // Normalize angle difference to -PI to PI
        float diffToFront = normalizeAngle(angleToReference - currentHeading);

        // Back angle is opposite (180 degrees / PI radians)
        float backAngle = angleToReference + static_cast<float>(M_PI);
        float diffToBack = normalizeAngle(backAngle - currentHeading);

        bool useBack = std::fabs(diffToBack) < std::fabs(diffToFront);
        SKSE::log::debug("GetClosestDirection: diffToFront={:.2f}, diffToBack={:.2f} -> {}",
            diffToFront, diffToBack, useBack ? "BACK" : "FRONT");
        return useBack;
    }

    // Rotate target to face toward or away from reference (whichever is closer)
    // Returns true if rotated to face away (back animation), false if facing toward (front animation)
    bool RotateTargetToClosest(RE::Actor* target, RE::Actor* reference) {
        bool useBack = GetClosestDirection(target, reference);

        if (!target || !reference) return useBack;

        float angleToReference = GetAngleBetween(target, reference);
        float backAngle = angleToReference + static_cast<float>(M_PI);
        float newAngle = useBack ? backAngle : angleToReference;

        // Normalize new angle to 0 to 2PI
        while (newAngle < 0) newAngle += 2.0f * static_cast<float>(M_PI);
        while (newAngle >= 2.0f * static_cast<float>(M_PI)) newAngle -= 2.0f * static_cast<float>(M_PI);

        SKSE::log::debug("RotateTargetToClosest: {} (new angle={:.2f})",
            useBack ? "BACK" : "FRONT", newAngle);

        // SetAngle takes NiPoint3 with x, y, z angles - we only change z (heading)
        RE::NiPoint3 angles = target->data.angle;
        angles.z = newAngle;
        target->SetAngle(angles);
        return useBack;
    }

    // Rotate attacker to face the target
    void RotateAttackerToTarget(RE::Actor* attacker, RE::Actor* target) {
        if (!attacker || !target) return;

        auto attackerPos = attacker->GetPosition();
        auto targetPos = target->GetPosition();

        float dx = targetPos.x - attackerPos.x;
        float dy = targetPos.y - attackerPos.y;

        // Calculate angle from attacker to target
        float angleToTarget = std::atan2(dx, dy);

        // Normalize angle to 0 to 2PI
        while (angleToTarget < 0) angleToTarget += 2.0f * static_cast<float>(M_PI);
        while (angleToTarget >= 2.0f * static_cast<float>(M_PI)) angleToTarget -= 2.0f * static_cast<float>(M_PI);

        SKSE::log::debug("RotateAttackerToTarget: rotating {} to face {} (angle={:.2f})",
            attacker->GetName(), target->GetName(), angleToTarget);

        // SetAngle takes NiPoint3 with x, y, z angles - we only change z (heading)
        RE::NiPoint3 angles = attacker->data.angle;
        angles.z = angleToTarget;
        attacker->SetAngle(angles);
    }

    // Check if player is facing the target within a given angle threshold
    // maxAngleDegrees: maximum angle from player's heading to target (default 90 = 180 degree cone in front)
    bool IsPlayerFacingTarget(RE::Actor* player, RE::Actor* target, float maxAngleDegrees) {
        if (!player || !target) return false;

        // Get angle from player to target
        float angleToTarget = GetAngleBetween(player, target);

        // Get player's current heading
        float playerHeading = player->GetAngleZ();

        // Calculate angle difference and normalize to -PI to PI
        float diff = angleToTarget - playerHeading;
        while (diff > static_cast<float>(M_PI)) diff -= 2.0f * static_cast<float>(M_PI);
        while (diff < -static_cast<float>(M_PI)) diff += 2.0f * static_cast<float>(M_PI);

        // Convert threshold to radians
        float maxAngleRad = maxAngleDegrees * (static_cast<float>(M_PI) / 180.0f);

        bool isFacing = std::fabs(diff) <= maxAngleRad;
        SKSE::log::debug("IsPlayerFacingTarget: angleToTarget={:.2f}, playerHeading={:.2f}, diff={:.2f}, threshold={:.2f} -> {}",
            angleToTarget, playerHeading, diff, maxAngleRad, isFacing ? "FACING" : "NOT FACING");

        return isFacing;
    }

    // Kill target actor (for lethal feeds)
    void KillTarget(RE::Actor* target) {
        if (!target) {
            SKSE::log::warn("KillTarget: target is null");
            return;
        }

        SKSE::log::info("KillTarget: scheduling kill for {}", target->GetName());

        // Schedule kill slightly after animation starts to allow feed mechanics to process
        SKSE::GetTaskInterface()->AddTask([targetHandle = target->GetHandle()]() {
            auto targetRef = targetHandle.get();
            if (targetRef) {
                auto* targetActor = targetRef->As<RE::Actor>();
                if (targetActor && !targetActor->IsDead()) {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    targetActor->KillImpl(player, 1.0f, true, true);
                    SKSE::log::info("Target killed: {}", targetActor->GetName());
                }
            }
        });
    }

    // Get hours since actor died
    // Returns -1.0f if actor is not dead or has no AI process
    float GetHoursSinceDeath(RE::Actor* actor) {
        if (!actor || !actor->IsDead()) {
            return -1.0f;
        }

        auto* process = actor->GetActorRuntimeData().currentProcess;
        if (!process) {
            SKSE::log::debug("GetHoursSinceDeath: {} has no AI process", actor->GetName());
            return -1.0f;
        }

        // Get death time from AI process
        float deathTime = process->deathTime;

        // Get current game time in hours
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) {
            SKSE::log::warn("GetHoursSinceDeath: Calendar singleton not available");
            return -1.0f;
        }

        float currentGameHours = calendar->GetHoursPassed();
        float hoursSinceDeath = currentGameHours - deathTime;

        SKSE::log::debug("GetHoursSinceDeath: {} - deathTime={:.2f}, currentHours={:.2f}, hoursSinceDeath={:.2f}",
            actor->GetName(), deathTime, currentGameHours, hoursSinceDeath);

        return hoursSinceDeath;
    }

    // Check if actor died recently (within maxHours)
    bool IsRecentlyDead(RE::Actor* actor, float maxHours) {
        float hoursSinceDeath = GetHoursSinceDeath(actor);
        if (hoursSinceDeath < 0.0f) {
            return false;  // Not dead or no process
        }
        bool isRecent = hoursSinceDeath <= maxHours;
        SKSE::log::debug("IsRecentlyDead: {} - {:.2f} hours since death, max={:.2f}, isRecent={}",
            actor->GetName(), hoursSinceDeath, maxHours, isRecent);
        return isRecent;
    }

    // Dead feed count tracking (in-memory, resets on game reload)
    static std::unordered_map<RE::FormID, int> g_DeadFeedCounts;
    static std::mutex g_DeadFeedCountsMutex;

    int GetDeadFeedCount(RE::Actor* actor) {
        if (!actor) return 0;
        std::lock_guard<std::mutex> lock(g_DeadFeedCountsMutex);
        auto it = g_DeadFeedCounts.find(actor->GetFormID());
        return (it != g_DeadFeedCounts.end()) ? it->second : 0;
    }

    void IncrementDeadFeedCount(RE::Actor* actor) {
        if (!actor) return;
        std::lock_guard<std::mutex> lock(g_DeadFeedCountsMutex);
        g_DeadFeedCounts[actor->GetFormID()]++;
        SKSE::log::debug("IncrementDeadFeedCount: {} now has {} feeds",
            actor->GetName(), g_DeadFeedCounts[actor->GetFormID()]);
    }

    bool HasExceededDeadFeedLimit(RE::Actor* actor, int maxFeeds) {
        if (maxFeeds <= 0) return false;  // 0 = unlimited
        int count = GetDeadFeedCount(actor);
        bool exceeded = count >= maxFeeds;
        if (exceeded) {
            SKSE::log::debug("HasExceededDeadFeedLimit: {} - count {} >= max {}",
                actor->GetName(), count, maxFeeds);
        }
        return exceeded;
    }

    // Check if attacker's attack should kill victim (uses game's ShouldAttackKill condition)
    // "Borrowed" from Pentalimbed
    bool ShouldAttackKill(const RE::Actor* attacker, const RE::Actor* victim) {
        static RE::TESConditionItem cond;
        static std::once_flag       flag;
        std::call_once(flag, [&]() {
            cond.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kShouldAttackKill;
            cond.data.flags.opCode          = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
            cond.data.comparisonValue.f     = 1.0f;
        });

        ConditionParam cond_param;
        cond_param.form                  = const_cast<RE::TESObjectREFR*>(victim->As<RE::TESObjectREFR>());
        cond.data.functionData.params[0] = std::bit_cast<void*>(cond_param);

        RE::ConditionCheckParams params(const_cast<RE::TESObjectREFR*>(attacker->As<RE::TESObjectREFR>()),
                                        const_cast<RE::TESObjectREFR*>(victim->As<RE::TESObjectREFR>()));
        return cond(params);
    }

    // Set or clear the kill move flag on an actor
    // This blocks Quick Loot and other activation during paired animations
    void SetInKillMove(RE::Actor* actor, bool inKillMove) {
        if (!actor) return;

        if (inKillMove) {
            actor->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kIsInKillMove);
        } else {
            actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kIsInKillMove);
        }
        SKSE::log::debug("SetInKillMove: {} = {}", actor->GetName(), inKillMove);
    }

    // Check if actor is currently in a kill move
    bool IsInKillMove(RE::Actor* actor) {
        if (!actor) return false;
        return actor->GetActorRuntimeData().boolFlags.all(RE::Actor::BOOL_FLAGS::kIsInKillMove);
    }

    // Check if actor is jumping
    bool IsJumping(RE::Actor* actor) {
        if (!actor) return false;

        // Safety: check if actor has valid 3D before accessing animation graph
        if (!actor->Is3DLoaded()) return false;

        bool res = false;
        actor->GetGraphVariableBool("bInJumpState", res);
        return res && actor->IsInMidair();
    }

    // Check if actor is riding a mount
    bool IsRiding(RE::Actor* actor) {
        if (!actor) return false;
        return actor->AsActorState()->actorState1.sitSleepState == RE::SIT_SLEEP_STATE::kRidingMount;
    }

    // Check if actor is swimming
    bool IsSwimming(RE::Actor* actor) {
        if (!actor) return false;
        return actor->AsActorState()->actorState1.swimming;
    }

    // =====================================================================
    // Idle Graph System Implementation
    // =====================================================================

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
            ctx.angleToTarget = GetAngleBetween(subject, target);
            ctx.distanceToTarget = subject->GetPosition().GetDistance(target->GetPosition());
            ctx.isBehindTarget = GetClosestDirection(target, subject);
        }

        return ctx;
    }

    // Cache of parent->children relationships (built once on first use)
    static std::unordered_map<RE::FormID, std::vector<RE::TESIdleForm*>> g_idleChildrenCache;
    static bool g_idleCacheBuilt = false;

    // Build the idle children cache by scanning all idles and checking their parentIdle
    static void BuildIdleChildrenCache() {
        if (g_idleCacheBuilt) return;

        SKSE::log::info("[IdleGraph] Building idle children cache from parentIdle relationships...");

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::error("[IdleGraph] TESDataHandler not available for cache building");
            return;
        }

        // Scan all idle forms in game
        int totalIdles = 0;
        int idlesWithParent = 0;

        // We need to iterate through all forms and find TESIdleForm
        // TESIdleForm form type is 78 (kIdleMarker in some versions)
        for (auto& formPair : dataHandler->formArrays) {
            // Skip if null
        }

        // Alternative: Use LookupByEditorID to find known roots and traverse from there
        // For now, let's try iterating all forms
        const auto& idleArray = dataHandler->GetFormArray<RE::TESIdleForm>();
        SKSE::log::info("[IdleGraph] Form array contains {} TESIdleForm entries", idleArray.size());

        for (auto* idle : idleArray) {
            if (!idle) continue;
            totalIdles++;

            auto* parent = idle->parentIdle;
            if (parent) {
                idlesWithParent++;
                g_idleChildrenCache[parent->GetFormID()].push_back(idle);
            }
        }

        SKSE::log::info("[IdleGraph] Cache built: {} total idles, {} have parents, {} unique parents",
            totalIdles, idlesWithParent, g_idleChildrenCache.size());

        g_idleCacheBuilt = true;
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

        SKSE::log::debug("[IdleGraph] Building node: '{}' (hasConditions={}, depth={})",
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
            SKSE::log::warn("[IdleGraph] Root idle '{}' not found", rootEditorID);
            return {};
        }

        return BuildIdleGraph(rootIdle, maxDepth);
    }

    // Evaluate if an idle's conditions pass for the given context
    bool EvaluateIdleConditions(RE::TESIdleForm* idle, const IdleSelectionContext& context) {
        if (!idle) return false;

        // No conditions = always passes
        if (!idle->conditions.head) {
            SKSE::log::debug("[IdleGraph] EvaluateConditions: '{}' has no conditions (auto-pass)",
                idle->GetFormEditorID() ? idle->GetFormEditorID() : "unknown");
            return true;
        }

        if (!context.subject) {
            SKSE::log::warn("[IdleGraph] EvaluateConditions: no subject in context");
            return false;
        }

        // Use vanilla condition evaluation
        // TESCondition::operator() takes (actionRef, targetRef)
        auto* subjectRef = context.subject->As<RE::TESObjectREFR>();
        auto* targetRef = context.target ? context.target->As<RE::TESObjectREFR>() : nullptr;

        bool result = idle->conditions(subjectRef, targetRef);

        SKSE::log::debug("[IdleGraph] EvaluateConditions: '{}' = {}",
            idle->GetFormEditorID() ? idle->GetFormEditorID() : "unknown",
            result ? "PASS" : "FAIL");

        return result;
    }

    // Internal helper for recursive selection
    static void SelectIdleRecursive(const IdleNode& node, const IdleSelectionContext& context,
                                    IdleSelectionResult& result, std::vector<std::string>& currentPath) {
        if (!node.idle) return;

        // Evaluate this node's conditions
        if (!EvaluateIdleConditions(node.idle, context)) {
            return;  // Conditions failed, don't continue down this branch
        }

        // Conditions passed - add to path
        currentPath.push_back(node.editorID);

        // This node is a valid candidate
        // We prefer deeper nodes (more specific), so always update result when we find a valid node
        result.selectedIdle = node.idle;
        result.editorID = node.editorID;
        result.success = true;
        result.selectionPath = currentPath;

        // Try to find an even more specific child
        for (const auto& child : node.children) {
            SelectIdleRecursive(child, context, result, currentPath);
        }

        currentPath.pop_back();
    }

    // Select the best idle from a graph based on conditions
    IdleSelectionResult SelectIdleFromGraph(const IdleNode& root, const IdleSelectionContext& context) {
        IdleSelectionResult result;
        result.success = false;

        if (!root.idle) {
            result.failureReason = "Invalid root node";
            return result;
        }

        std::vector<std::string> currentPath;
        SelectIdleRecursive(root, context, result, currentPath);

        if (result.success) {
            SKSE::log::info("[IdleGraph] Selected idle: '{}' via path: {}",
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
            SKSE::log::warn("[IdleGraph] No idle selected from graph rooted at '{}'", root.editorID);
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
        float angleToTarget = GetAngleBetween(attacker, target);

        // Get attacker's facing direction
        float attackerHeading = attacker->GetAngleZ();

        // Calculate the attack angle (difference between facing and target direction)
        float attackAngle = normalizeAngle(angleToTarget - attackerHeading);

        return attackAngle;
    }

    // =====================================================================
    // Debug / Logging Utilities Implementation
    // =====================================================================

    // Log the idle graph structure to SKSE log
    // Prints nodes with conditions, highlighting HasKeyword(ActorTypeNPC) conditions
    void LogIdleGraph(const IdleNode& node, int indent) {
        if (!node.idle) return;

        // Check if this node has ActorTypeNPC keyword condition
        bool hasActorTypeNPC = false;
        if (node.hasConditions) {
            auto* condItem = node.idle->conditions.head;
            while (condItem) {
                auto funcID = condItem->data.functionData.function.underlying();
                // HasKeyword = 448
                if (funcID == 448) {
                    // Check if the keyword parameter is ActorTypeNPC
                    auto* param = reinterpret_cast<RE::TESForm*>(condItem->data.functionData.params[0]);
                    if (param) {
                        const char* editorID = param->GetFormEditorID();
                        if (editorID && std::string(editorID).find("ActorTypeNPC") != std::string::npos) {
                            hasActorTypeNPC = true;
                            break;
                        }
                    }
                }
                condItem = condItem->next;
            }
        }

        bool hasChildren = !node.children.empty();

        // Print nodes that have children OR have ActorTypeNPC condition
        if (!hasChildren && !hasActorTypeNPC) {
            // Still recurse children even if we don't print this node
            for (const auto& child : node.children) {
                LogIdleGraph(child, indent + 1);
            }
            return;
        }

        std::string indentStr(indent * 2, ' ');
        std::string condStr = node.hasConditions ? " [HAS CONDITIONS]" : "";
        std::string npcStr = hasActorTypeNPC ? " *** ActorTypeNPC ***" : "";

        SKSE::log::info("{}[IDLE] '{}' (FormID: {:08X}) Children: {}{}{}",
            indentStr,
            node.editorID.empty() ? "<unnamed>" : node.editorID,
            node.idle->GetFormID(),
            node.children.size(),
            condStr,
            npcStr);

        // If this has ActorTypeNPC, print all condition details
        if (hasActorTypeNPC) {
            auto* condItem = node.idle->conditions.head;
            int condIndex = 0;
            while (condItem) {
                auto funcID = condItem->data.functionData.function.underlying();
                auto opCode = static_cast<int>(condItem->data.flags.opCode);
                float compValue = condItem->data.comparisonValue.f;
                bool isOR = condItem->data.flags.isOR;

                std::string paramStr;
                auto* param = reinterpret_cast<RE::TESForm*>(condItem->data.functionData.params[0]);
                if (param) {
                    const char* paramEditorID = param->GetFormEditorID();
                    paramStr = paramEditorID ? paramEditorID : "<no editorID>";
                }

                SKSE::log::info("{}  Cond[{}]: FuncID={}, Param='{}', Op={}, Value={:.2f}, {}",
                    indentStr, condIndex, funcID, paramStr, opCode, compValue,
                    isOR ? "OR" : "AND");

                condItem = condItem->next;
                condIndex++;
            }
        }

        // List child names for quick reference
        if (hasChildren) {
            std::string childNames;
            for (size_t i = 0; i < node.children.size(); ++i) {
                if (i > 0) childNames += ", ";
                childNames += node.children[i].editorID.empty() ? "<unnamed>" : node.children[i].editorID;
            }
            SKSE::log::info("{}  -> Children: [{}]", indentStr, childNames);
        }

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
}
