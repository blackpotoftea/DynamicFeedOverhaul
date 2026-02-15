#include "utils/AnimUtil.h"
#include "feed/TargetState.h"
#include "feed/CustomFeed.h"
#include "Settings.h"
#include "papyrus/PapyrusCall.h"
#include <memory>
#include <mutex>
#include <unordered_set>

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
                a->SetGraphVariableFloat("OStimSpeed", playbackSpeed);
                a->NotifyAnimationGraph(animation);
            }
        });
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
            if (targetHandle) {
                auto targetRefPtr = targetHandle.get();
                if (!targetRefPtr) {
                    SKSE::log::warn("[AnimUtil::playIdle] Target handle invalid for {}, playing without target", actorName);
                } else {
                    // Target can be TESObjectREFR (for furniture/beds) or Actor, so we use .get()
                    t = targetRefPtr.get();
                }
            }

            auto* process = a->GetActorRuntimeData().currentProcess;
            if (!process) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: No process for {}", actorName);
                return;
            }

            bool success = process->PlayIdle(a, idle, t);
            if (success) {
                SKSE::log::info("[AnimUtil::playIdle] SUCCESS: Idle {:X} started on {} (target: {})",
                    idleFormID, actorName, targetName);
            } else {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: PlayIdle returned false for {} (idle: {:X})",
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

                    a->SetGraphVariableFloat("OStimSpeed", 1.0f);
                    a->SetGraphVariableBool("bHumanoidFootIKDisable", false);
                    a->SetGraphVariableBool("bHeadTrackSpine", true);
                    a->SetGraphVariableBool("bHeadTracking", true);
                    a->SetGraphVariableBool("tdmHeadtrackingBehavior", true);

                    a->NotifyAnimationGraph("IdleForceDefaultState");
                }
            });
        } else {
            // NPC handling
            SKSE::GetTaskInterface()->AddTask([actorHandle] {
                auto actorRef = actorHandle.get();
                if (auto* a = actorRef.get()) {
                    a->SetGraphVariableFloat("OStimSpeed", 1.0f);
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

        if (isVampire && !isVampireLord && !isWerewolf && settings->General.CheckHungerStage) {
            // Combat targets can bypass hunger check if IgnoreHungerCheck is enabled
            if (targetInCombat && settings->Combat.IgnoreHungerCheck) {
                return true;
            }

            int vampireStage = PapyrusCall::GetVampireStage();
            if (vampireStage < settings->General.MinHungerStage) {
                return false;
            }
        }

        return true;
    }

    // Animation selection and state checking (moved from PairedAnimPromptSink)
    bool IsInPairedAnimation(RE::Actor* actor) {
        if (!actor) return false;
        bool result = false;
        actor->GetGraphVariableBool("bIsSynced", result);
        if (result) return true;
        actor->GetGraphVariableBool("bInKillMove", result);
        if (result) return true;
        return false;
    }

    int DetermineTargetState(RE::Actor* target, bool& outIsInCombat) {
        outIsInCombat = target->IsInCombat();
        if (outIsInCombat) return AnimUtil::kCombat;
        else if (TargetState::IsSleeping(target)) return AnimUtil::kSleeping;
        else if (TargetState::IsSitting(target)) return AnimUtil::kSitting;
        return AnimUtil::kStanding;
    }

    void ApplyHeightAdjustment(RE::PlayerCharacter* player, RE::Actor* target, float minHeightDiff, float maxHeightDiff) {
        auto playerPos = player->GetPosition();
        auto targetPos = target->GetPosition();
        float heightDiff = std::fabs(targetPos.z - playerPos.z);

        if (heightDiff <= minHeightDiff) return;

        if (heightDiff > maxHeightDiff) {
            SKSE::log::warn("Height diff {:.1f} exceeds max {:.1f} - skipping repositioning",
                heightDiff, maxHeightDiff);
            return;
        }

        float higherZ = std::max(playerPos.z, targetPos.z);
        if (playerPos.z < targetPos.z) {
            SKSE::log::debug("Height diff: {:.1f} - moving player up", heightDiff);
            player->SetPosition(RE::NiPoint3(playerPos.x, playerPos.y, higherZ), true);
        } else {
            SKSE::log::debug("Height diff: {:.1f} - moving target up", heightDiff);
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
                return CustomFeed::Idles::WEREWOLF_STANDING_FRONT;
            }
            if (TargetState::IsVampireLord(player)) {
                SKSE::log::debug("Player is Vampire Lord - using VL feed");
                return isBehind ? CustomFeed::Idles::VAMPIRELORD_STANDING_BACK : CustomFeed::Idles::VAMPIRELORD_STANDING_FRONT;
            }
        }

        if (targetState == AnimUtil::kSleeping && furnitureRef) {
            outIsPairedAnim = false;
            bool isLeft = CustomFeed::IsPlayerOnLeftSide(target);
            bool isBedroll = CustomFeed::IsBedroll(furnitureRef.get());

            if (isBedroll) {
                SKSE::log::debug("Bedroll {} side (solo idle)", isLeft ? "left" : "right");
                return isLeft ? CustomFeed::Idles::VAMPIRE_BEDROLL_LEFT : CustomFeed::Idles::VAMPIRE_BEDROLL_RIGHT;
            } else {
                SKSE::log::debug("Bed {} side (solo idle)", isLeft ? "left" : "right");
                return isLeft ? CustomFeed::Idles::VAMPIRE_BED_LEFT : CustomFeed::Idles::VAMPIRE_BED_RIGHT;
            }
        }

        const char* posStr = isBehind ? "back" : "front";

        if (targetState == AnimUtil::kSitting) {
            SKSE::log::debug("Sitting {} feed", posStr);
            return isBehind ? CustomFeed::Idles::VAMPIRE_SITTING_BACK : CustomFeed::Idles::VAMPIRE_SITTING_FRONT;
        } else {
            SKSE::log::debug("{} {} feed", targetState == AnimUtil::kCombat ? "Combat" : "Standing", posStr);
            return isBehind ? CustomFeed::Idles::VAMPIRE_STANDING_BACK : CustomFeed::Idles::VAMPIRE_STANDING_FRONT;
        }
    }

    // Animation graph variable management (moved from PairedAnimPromptSink)
    void SetFeedGraphVars(RE::Actor* actor, int feedType) {
        if (!actor) return;

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

        constexpr auto IsSkyPromptFeeding = "IsSkyPromptFeeding";
        constexpr auto SkyPromptFeedType = "SkyPromptFeedType";

        actor->SetGraphVariableBool(IsSkyPromptFeeding, false);
        actor->SetGraphVariableInt(SkyPromptFeedType, 0);
        SKSE::log::debug("Cleared feed graph vars on {}", actor->GetName());
    }

    // Calculate angle from target to player (in radians)
    float GetAngleToPlayer(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return 0.0f;

        auto playerPos = player->GetPosition();
        auto targetPos = target->GetPosition();

        float dx = playerPos.x - targetPos.x;
        float dy = playerPos.y - targetPos.y;

        // atan2 gives angle from target to player
        return std::atan2(dx, dy);
    }

    // Get closest direction (front/back) without rotating target
    // Returns true if back is closer, false if front is closer
    bool GetClosestDirection(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return false;

        float angleToPlayer = GetAngleToPlayer(target);
        float currentHeading = target->GetAngleZ();

        // Normalize angle difference to -PI to PI
        float diffToFront = normalizeAngle(angleToPlayer - currentHeading);

        // Back angle is opposite (180 degrees / PI radians)
        float backAngle = angleToPlayer + static_cast<float>(M_PI);
        float diffToBack = normalizeAngle(backAngle - currentHeading);

        bool useBack = std::fabs(diffToBack) < std::fabs(diffToFront);
        SKSE::log::debug("GetClosestDirection: diffToFront={:.2f}, diffToBack={:.2f} -> {}",
            diffToFront, diffToBack, useBack ? "BACK" : "FRONT");
        return useBack;
    }

    // Rotate target to face toward or away from player (whichever is closer)
    // Returns true if rotated to face away (back animation), false if facing toward (front animation)
    bool RotateTargetToClosest(RE::Actor* target) {
        bool useBack = GetClosestDirection(target);

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return useBack;

        float angleToPlayer = GetAngleToPlayer(target);
        float backAngle = angleToPlayer + static_cast<float>(M_PI);
        float newAngle = useBack ? backAngle : angleToPlayer;

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

    // Rotate player to face the target
    void RotatePlayerToTarget(RE::Actor* target) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return;

        auto playerPos = player->GetPosition();
        auto targetPos = target->GetPosition();

        float dx = targetPos.x - playerPos.x;
        float dy = targetPos.y - playerPos.y;

        // Calculate angle from player to target
        float angleToTarget = std::atan2(dx, dy);

        // Normalize angle to 0 to 2PI
        while (angleToTarget < 0) angleToTarget += 2.0f * static_cast<float>(M_PI);
        while (angleToTarget >= 2.0f * static_cast<float>(M_PI)) angleToTarget -= 2.0f * static_cast<float>(M_PI);

        SKSE::log::debug("RotatePlayerToTarget: rotating player to face {} (angle={:.2f})",
            target->GetName(), angleToTarget);

        // SetAngle takes NiPoint3 with x, y, z angles - we only change z (heading)
        RE::NiPoint3 angles = player->data.angle;
        angles.z = angleToTarget;
        player->SetAngle(angles);
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
}
