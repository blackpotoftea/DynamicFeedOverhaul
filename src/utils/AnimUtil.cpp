#include "utils/AnimUtil.h"
#include "feed/TargetState.h"
#include "feed/PairedAnimPromptSink.h"
#include "feed/AnimationRegistry.h"
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

    // Helper to get NiAVObject from hkpCdBody
    RE::NiAVObject* GetAVObjectFromBody(const RE::hkpCdBody* body) {
        if (!body) return nullptr;
        using func_t = RE::NiAVObject* (*)(const RE::hkpCdBody*);
        static REL::Relocation<func_t> func{RELOCATION_ID(76160, 77988)};
        return func(body);
    }

    // Ray collector that stores all hits for later processing
    // Allows caller to find highest hit (for stair detection) rather than closest
    class RayCollector : public RE::hkpRayHitCollector {
    public:
        struct HitResult {
            RE::NiPoint3 normal;
            float hitFraction;
            const RE::hkpCdBody* body;

            RE::NiAVObject* getAVObject() const {
                return body ? GetAVObjectFromBody(body) : nullptr;
            }
        };

        RE::hkpWorldRayCastOutput rayHit;  // 0x10 - matches hkpClosestRayHitCollector layout
        std::vector<RE::NiAVObject*> objectFilter;
        std::vector<HitResult> hits;

        RayCollector() {
            Reset();
        }

        ~RayCollector() override = default;

        void AddFilter(RE::NiAVObject* obj) {
            if (obj) objectFilter.push_back(obj);
        }

        void AddRayHit(const RE::hkpCdBody& body, const RE::hkpShapeRayCastCollectorOutput& hitInfo) override {
            // Get the root body
            const RE::hkpCdBody* rootBody = &body;
            while (rootBody->parent) {
                rootBody = rootBody->parent;
            }

            if (!rootBody) return;

            // Check if this object is in the filter list (skip actors we want to ignore)
            auto* avObject = GetAVObjectFromBody(rootBody);
            for (const auto* filteredObj : objectFilter) {
                if (avObject == filteredObj) return;
            }

            // Store the hit for later processing
            HitResult hit;
            hit.normal.x = hitInfo.normal.quad.m128_f32[0];
            hit.normal.y = hitInfo.normal.quad.m128_f32[1];
            hit.normal.z = hitInfo.normal.quad.m128_f32[2];
            hit.hitFraction = hitInfo.hitFraction;
            hit.body = rootBody;

            // Update early out to allow finding more hits
            earlyOutHitFraction = hitInfo.hitFraction;
            hits.push_back(hit);
        }

        const std::vector<HitResult>& GetHits() const {
            return hits;
        }

        void Reset() {
            hkpRayHitCollector::Reset();
            rayHit.Reset();
            hits.clear();
            objectFilter.clear();
        }
    };
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
    // callbackTarget: actor to pass to callback (for integration), may differ from animation target
    // isPaired: if true, use callbackTarget for PlayIdle; if false, play solo animation but still pass target to callback
    void playIdle(RE::Actor* actor, RE::TESIdleForm* idle, RE::TESObjectREFR* callbackTarget,
                  PlayIdleCallback callback, bool isPaired) {
        if (!actor || !idle) {
            SKSE::log::warn("[AnimUtil::playIdle] Invalid input: actor={}, idle={}",
                actor ? "valid" : "null", idle ? "valid" : "null");
            if (callback) {
                callback(false, nullptr);
            }
            return;
        }

        auto actorHandle = actor->CreateRefHandle();
        auto idleFormID = idle->GetFormID();
        auto callbackTargetHandle = callbackTarget ? callbackTarget->CreateRefHandle() : RE::ObjectRefHandle();
        auto actorName = actor->GetName() ? std::string(actor->GetName()) : std::string("sourceActorNameNone");
        auto callbackTargetName = callbackTarget ? std::string(callbackTarget->GetName()) : std::string("callbackTargetNameNone");

        SKSE::log::debug("[AnimUtil::playIdle] Queuing idle {:X} for {} (callbackTarget: {}, isPaired: {})",
            idleFormID, actorName, callbackTargetName, isPaired);

        SKSE::GetTaskInterface()->AddTask([actorHandle, idleFormID, callbackTargetHandle, actorName, callbackTargetName, callback, isPaired] {
            // 1. Resolve the handle to get NiPointer<TESObjectREFR>
            auto refPtr = actorHandle.get();
            if (!refPtr) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: Actor handle invalid for {}", actorName);
                if (callback) {
                    callback(false, nullptr);
                }
                return;
            }

            // 2. Cast the generic TESObjectREFR to Actor* (critical for accessing currentProcess)
            auto* a = refPtr->As<RE::Actor>();
            if (!a) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: Object is not an Actor for {}", actorName);
                if (callback) {
                    callback(false, nullptr);
                }
                return;
            }

            auto* idle = RE::TESForm::LookupByID<RE::TESIdleForm>(idleFormID);
            if (!idle) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: Idle form {:X} not found for {}", idleFormID, actorName);
                if (callback) {
                    callback(false, nullptr);
                }
                return;
            }

            RE::TESObjectREFR* animTarget = nullptr;
            RE::Actor* callbackTargetActor = nullptr;
            if (callbackTargetHandle) {
                auto callbackTargetRefPtr = callbackTargetHandle.get();
                if (!callbackTargetRefPtr) {
                    SKSE::log::warn("[AnimUtil::playIdle] Callback target handle invalid for {}", actorName);
                } else {
                    // Target can be TESObjectREFR (for furniture/beds) or Actor, so we use .get()
                    callbackTargetActor = callbackTargetRefPtr->As<RE::Actor>();
                    // Only use as animation target if isPaired
                    if (isPaired) {
                        animTarget = callbackTargetRefPtr.get();
                    }
                }
            }

            // Preprocess for paired animations - clear stagger/attack/knockdown states
            if (isPaired && callbackTargetActor) {
                SKSE::log::debug("[AnimUtil::playIdle] Preprocessing actors for paired idle");
                PrepareActorForPairedIdle(a);
                PrepareActorForPairedIdle(callbackTargetActor);
            }

            auto* process = a->GetActorRuntimeData().currentProcess;
            if (!process) {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: No process for {}", actorName);
                if (callback) {
                    callback(false, callbackTargetActor);
                }
                return;
            }

            // Clear conditions on idle and all parents to bypass condition checks
            // IdleParser::ClearIdleConditions(idle);

            bool success = process->PlayIdle(a, idle, animTarget);

            // Restore conditions immediately after PlayIdle call
            // IdleParser::RestoreIdleConditions();
            // bool success = _playPairedIdle(process, a, RE::DEFAULT_OBJECT::kActionIdle, idle, true, false, animTarget);
            if (success) {
                SKSE::log::info("[AnimUtil::playIdle] SUCCESS: Idle {:X} started on {} (callbackTarget: {}, isPaired: {})",
                    idleFormID, actorName, callbackTargetName, isPaired);
            } else {
                SKSE::log::error("[AnimUtil::playIdle] FAILED: PlayIdle returned false for {} (idle: {:X})",
                    actorName, idleFormID);
                // Immediately mark feed as ended so timeout resets without waiting 15s
                // FeedAnimState uses atomics internally so this is thread-safe
                FeedAnimState::MarkFeedEnded();
                AnimEventSink::Unregister();
            }

            // Invoke callback with result (called on game thread)
            // Always pass callbackTargetActor regardless of isPaired
            if (callback) {
                callback(success, callbackTargetActor);
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
            return Feed::kDead;
        }
        outIsInCombat = target->IsInCombat();
        if (outIsInCombat) return Feed::kCombat;
        else if (TargetState::IsSleeping(target)) return Feed::kSleeping;
        else if (TargetState::IsSitting(target)) return Feed::kSitting;
        return Feed::kStanding;
    }

    // Helper function to cast a single ground ray and process hits
    // Returns GroundHit with highest valid ground surface from all collected hits
    static GroundHit CastGroundRay(RE::bhkWorld* bhkWorld, const RE::NiPoint3& position,
                                    RayCollector& collector, float startOffset, float searchDepth) {
        GroundHit result;

        // Havok scale factor
        constexpr float hkpScale = 0.0142875f;

        // Ray start slightly above position, ray end below
        RE::NiPoint3 rayStart = { position.x, position.y, position.z + startOffset };
        RE::NiPoint3 rayDir = { 0.0f, 0.0f, -(startOffset + searchDepth) };

        RE::NiPoint3 from = rayStart * hkpScale;
        RE::NiPoint3 dir = rayDir * hkpScale;

        // Set up pick data - rayInput.to is zero, direction goes in ray member
        RE::bhkPickData pickData;
        pickData.rayInput.from.quad.m128_f32[0] = from.x;
        pickData.rayInput.from.quad.m128_f32[1] = from.y;
        pickData.rayInput.from.quad.m128_f32[2] = from.z;
        pickData.rayInput.from.quad.m128_f32[3] = 1.0f;

        pickData.rayInput.to.quad.m128_f32[0] = 0.0f;
        pickData.rayInput.to.quad.m128_f32[1] = 0.0f;
        pickData.rayInput.to.quad.m128_f32[2] = 0.0f;
        pickData.rayInput.to.quad.m128_f32[3] = 0.0f;

        pickData.ray.quad.m128_f32[0] = dir.x;
        pickData.ray.quad.m128_f32[1] = dir.y;
        pickData.ray.quad.m128_f32[2] = dir.z;
        pickData.ray.quad.m128_f32[3] = 1.0f;

        pickData.rayHitCollectorA8 = reinterpret_cast<RE::hkpClosestRayHitCollector*>(&collector);

        bhkWorld->PickObject(pickData);

        // Process all hits to find the highest valid ground surface
        const auto& hits = collector.GetHits();
        for (const auto& hit : hits) {
            // Filter by normal: must be pointing upward (ground, not wall/ceiling)
            // normalZ > 0.1 allows slopes up to ~84 degrees
            if (hit.normal.z < 0.1f) continue;

            // Calculate ground Z from hit fraction
            float groundZ = rayStart.z + rayDir.z * hit.hitFraction;

            // Only accept hits AT or BELOW position (not ceilings/overhangs above)
            if (groundZ > position.z + 1.0f) continue;

            // Take the HIGHEST valid ground hit (actor stands on highest surface)
            if (!result.hit || groundZ > result.groundZ) {
                result.hit = true;
                result.groundZ = groundZ;
                result.hitNormal = hit.normal;
                result.surface = hit.getAVObject();

                // Try to get reference from surface
                if (result.surface && result.surface->GetUserData()) {
                    result.ref = result.surface->GetUserData();
                }
            }
        }

        return result;
    }

    GroundHit GetGroundHeight(RE::Actor* actor) {
        GroundHit result;

        if (!actor) {
            SKSE::log::warn("GetGroundHeight: actor is null");
            return result;
        }

        auto* cell = actor->GetParentCell();
        if (!cell) {
            SKSE::log::warn("GetGroundHeight: {} has no parent cell", actor->GetName());
            return result;
        }

        auto* bhkWorld = cell->GetbhkWorld();
        if (!bhkWorld) {
            SKSE::log::warn("GetGroundHeight: no bhkWorld for cell");
            return result;
        }

        const auto position = actor->GetPosition();

        // Multi-ray fan pattern configuration
        constexpr float spread = 20.0f;      // Half shoulder width (~40 unit total spread)
        constexpr float startOffset = 5.0f;  // Small upward bias to clear minor irregularities
        constexpr float searchDepth = 150.0f; // Search distance below actor

        // 5-ray offsets: center + 4 compass points
        struct Offset { float x, y; };
        const Offset offsets[] = {
            { 0.0f,    0.0f    },  // center
            { spread,  0.0f    },  // forward (X+)
            {-spread,  0.0f    },  // back (X-)
            { 0.0f,    spread  },  // right (Y+)
            { 0.0f,   -spread  },  // left (Y-)
        };

        // Set up collector with actor filter (to avoid self-collision)
        RayCollector collector;
        if (auto* actor3D = actor->Get3D()) {
            collector.AddFilter(actor3D);
        }

        // Cast all 5 rays and find the best (highest) ground hit
        GroundHit bestHit;
        for (const auto& offset : offsets) {
            RE::NiPoint3 rayPos = { position.x + offset.x, position.y + offset.y, position.z };

            // Reset collector for each ray but keep the filter
            auto savedFilter = std::move(collector.objectFilter);
            collector.Reset();
            collector.objectFilter = std::move(savedFilter);

            GroundHit hit = CastGroundRay(bhkWorld, rayPos, collector, startOffset, searchDepth);

            if (hit.hit) {
                // Take HIGHEST hit - that's the surface actor is actually standing on
                if (!bestHit.hit || hit.groundZ > bestHit.groundZ) {
                    bestHit = hit;
                }
            }
        }

        if (bestHit.hit) {
            // Calculate slope angle for logging
            float slopeAngle = std::acos(bestHit.hitNormal.z) * (180.0f / static_cast<float>(M_PI));
            SKSE::log::debug("GetGroundHeight: {} - groundZ={:.2f}, actorZ={:.2f}, slope={:.1f}deg",
                actor->GetName(), bestHit.groundZ, position.z, slopeAngle);
        } else {
            SKSE::log::debug("GetGroundHeight: {} - no ground detected", actor->GetName());
        }

        return bestHit;
    }

    void ApplyHeightAdjustment(RE::Actor* attacker, RE::Actor* target, float minHeightDiff, float maxHeightDiff) {
        auto attackerPos = attacker->GetPosition();
        auto targetPos = target->GetPosition();
        float heightDiff = std::fabs(targetPos.z - attackerPos.z);

        SKSE::log::info("ApplyHeightAdjustment: heightDiff={:.2f}, min={:.2f}, max={:.2f}",
            heightDiff, minHeightDiff, maxHeightDiff);

        // Get ground heights using new multi-ray detection
        GroundHit attackerGround = GetGroundHeight(attacker);
        GroundHit targetGround = GetGroundHeight(target);

        // Log ground detection results
        if (attackerGround.hit && targetGround.hit) {
            float groundHeightDiff = std::fabs(attackerGround.groundZ - targetGround.groundZ);
            SKSE::log::info("ApplyHeightAdjustment: groundHeightDiff={:.2f} (attacker={:.2f}, target={:.2f})",
                groundHeightDiff, attackerGround.groundZ, targetGround.groundZ);
        } else {
            SKSE::log::warn("ApplyHeightAdjustment: ground detection failed (attacker={}, target={})",
                attackerGround.hit ? "ok" : "FAILED", targetGround.hit ? "ok" : "FAILED");
        }

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

        // Use ground heights if available, otherwise fall back to actor Z
        float attackerZ = attackerGround.hit ? attackerGround.groundZ : attackerPos.z;
        float targetZ = targetGround.hit ? targetGround.groundZ : targetPos.z;
        float higherZ = std::max(attackerZ, targetZ);

        if (attackerZ < targetZ) {
            float newZ = attackerPos.z + (higherZ - attackerZ);
            SKSE::log::info("ApplyHeightAdjustment: Moving attacker UP from {:.2f} to {:.2f} (ground diff: {:.2f})",
                attackerPos.z, newZ, higherZ - attackerZ);
            attacker->SetPosition(RE::NiPoint3(attackerPos.x, attackerPos.y, newZ), true);
        } else {
            float newZ = targetPos.z + (higherZ - targetZ);
            SKSE::log::info("ApplyHeightAdjustment: Moving target UP from {:.2f} to {:.2f} (ground diff: {:.2f})",
                targetPos.z, newZ, higherZ - targetZ);
            target->SetPosition(RE::NiPoint3(targetPos.x, targetPos.y, newZ), true);
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
}
