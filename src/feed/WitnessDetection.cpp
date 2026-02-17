#include "WitnessDetection.h"
#include "Settings.h"
#include "TargetState.h"

namespace {
    // Custom relocation for SendAssaultAlarm function not exposed in CommonLibSSE
    // 1405DE810       1406042C0
    // Monitor221hz
    void SendAssaultAlarm(RE::Actor* a_victim, RE::Actor* a_assaulter, bool arg3) {
        using func_t = decltype(SendAssaultAlarm);
        REL::Relocation<func_t> func{ RELOCATION_ID(36429, 37424) };
        func(a_victim, a_assaulter, arg3);
    }
}

namespace WitnessDetection {

    std::int32_t GetDetectionLevel(RE::Actor* detector, RE::Actor* target) {
        if (!detector || !target) return 0;

        // Use RequestDetectionLevel to get the detection level
        std::int32_t level = detector->RequestDetectionLevel(target, RE::DETECTION_PRIORITY::kNormal);

        if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] GetDetectionLevel: {} detecting {} = {}",
                detector->GetName(), target->GetName(), level);
        }

        return level;
    }

    bool CanActorWitnessFeed(RE::Actor* potentialWitness, RE::Actor* player, RE::Actor* target) {
        if (!potentialWitness || !player || !target) return false;

        // Skip if same as player or target
        if (potentialWitness == player || potentialWitness == target) return false;

        // Skip if witness is a teammate/follower of the player (they shouldn't report you)
        if (potentialWitness->IsPlayerTeammate()) {
            if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
                SKSE::log::trace("[WitnessDetection] {} is player teammate, skipping", potentialWitness->GetName());
            }
            return false;
        }

        // Skip if dead or disabled
        if (potentialWitness->IsDead() || potentialWitness->IsDisabled()) {
            if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
                SKSE::log::trace("[WitnessDetection] {} is dead/disabled, skipping", potentialWitness->GetName());
            }
            return false;
        }

        // Check if actor can see the player or target (HasLineOfSight requires reference parameter)
        bool losResult1 = false;
        bool losResult2 = false;
        bool canSeePlayer = potentialWitness->HasLineOfSight(player, losResult1);
        bool canSeeTarget = potentialWitness->HasLineOfSight(target, losResult2);

        if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] {} LOS check: canSeePlayer={} ({}), canSeeTarget={} ({})",
                potentialWitness->GetName(), canSeePlayer, losResult1, canSeeTarget, losResult2);
        }

        if (!canSeePlayer && !canSeeTarget) {
            if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] {} has no LOS to player or target", potentialWitness->GetName());
            }
            return false;
        }

        // Check detection level - has this actor actually detected the player?
        std::int32_t detectionLevel = GetDetectionLevel(potentialWitness, player);

        // Detection levels: 0=None, 1-25=Noticed, 26-50=Unseen, 51-75=Seen, 76-100=Fully Detected
        // Check if at least "Seen" level
        if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] {} detection level: {} (threshold: 50)",
                potentialWitness->GetName(), detectionLevel);
        }

        if (detectionLevel >= 50) {
            SKSE::log::warn("[WitnessDetection] Actor {} has DETECTED player (level: {})",
                potentialWitness->GetName(), detectionLevel);
            return true;
        }

        // Alternative check: hostile actor in combat with player
        bool isHostile = potentialWitness->IsHostileToActor(player);
        bool inCombat = potentialWitness->IsInCombat();

        if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] {} hostile check: isHostile={}, inCombat={}",
                potentialWitness->GetName(), isHostile, inCombat);
        }

        if (isHostile && inCombat) {
            SKSE::log::warn("[WitnessDetection] Hostile actor {} is in combat with player",
                potentialWitness->GetName());
            return true;
        }

        if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] {} is NOT a valid witness", potentialWitness->GetName());
        }
        return false;
    }

    RE::Actor* CheckForWitnesses(RE::Actor* player, RE::Actor* target) {
        if (!player || !target) {
            if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] player or target is null");
            }
            return nullptr;
        }

        auto* settings = Settings::GetSingleton();
        if (!settings->Combat.EnableWitnessDetection) {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] Witness detection disabled in settings");
            }
            return nullptr;
        }

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] ProcessLists is null");
            }
            return nullptr;
        }

        auto playerPos = player->GetPosition();
        float detectionRadius = settings->Combat.WitnessDetectionRadius;

        if (settings->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] Scanning {} actors within radius {:.1f}",
                processLists->highActorHandles.size(), detectionRadius);
        }

        int checkedCount = 0;
        int withinRadiusCount = 0;

        // Check all nearby high-priority actors
        for (auto& actorHandle : processLists->highActorHandles) {
            auto actorPtr = actorHandle.get();
            if (!actorPtr) continue;

            auto* actor = actorPtr.get();
            if (!actor) continue;

            checkedCount++;

            // Check if actor is within detection radius
            float distance = actor->GetPosition().GetDistance(playerPos);
            if (distance > detectionRadius) continue;

            withinRadiusCount++;

            // Check if this actor can witness the feed
            if (CanActorWitnessFeed(actor, player, target)) {
                SKSE::log::warn("[WitnessDetection] Feed witnessed by: {} (distance: {:.1f})",
                    actor->GetName(), distance);
                return actor;
            }
        }

        if (settings->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] Checked {} actors, {} within radius, no witnesses found",
                checkedCount, withinRadiusCount);
        }

        return nullptr;  // No witnesses found
    }

    void PerformWitnessCheck(RE::Actor* player, RE::Actor* target) {
        if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] PerformWitnessCheck called");
        }

        auto* settings = Settings::GetSingleton();

        if (!player) {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] no player");
            }
            return;
        }

        if (!target) {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] no target");
            }
            return;
        }

        // Skip if target died or became invalid
        if (target->IsDead() || target->IsDisabled()) {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] target is dead or disabled, skipping");
            }
            return;
        }

        if (settings->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] checking for witnesses (player: {}, target: {})",
                player->GetName(), target->GetName());
        }

        // Check if the victim themselves should raise alarm (if awake and not a follower)
        if (!target->IsPlayerTeammate()) {
            // Check if victim is conscious and aware (not sleeping, unconscious, or bleeding out)
            if (TargetState::IsConsciousAndAware(target)) {
                if (settings->Combat.WitnessDebugLogging) {
                    SKSE::log::warn("[WitnessDetection] Victim {} is conscious and not a teammate - raising alarm",
                        target->GetName());
                }
                OnDetectedByWitness(player, target, target);  // Victim is their own witness
                return;  // No need to check other witnesses if victim already alarmed
            }
        }

        // Use the WitnessDetection module to check for witnesses
        RE::Actor* witness = CheckForWitnesses(player, target);
        if (witness) {
            SKSE::log::warn("[WitnessDetection] found witness {}", witness->GetName());
            OnDetectedByWitness(player, target, witness);
        } else {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] no witnesses found");
            }
        }
    }

    void OnDetectedByWitness(RE::Actor* player, RE::Actor* target, RE::Actor* witness) {
        if (!player || !target || !witness) return;

        // Cooldown check: prevent bounty spam if detected multiple times rapidly
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;

        static float lastWitnessDetectionTime = 0.0f;
        float currentTime = processLists->GetSystemTimeClock();
        constexpr float cooldownDuration = 5.0f;  // 5 seconds between bounty additions

        if (currentTime - lastWitnessDetectionTime < cooldownDuration) {
            SKSE::log::debug("[WitnessDetection] Witness detection on cooldown, skipping");
            return;
        }

        lastWitnessDetectionTime = currentTime;
        SKSE::log::warn("[WitnessDetection] Feed witnessed by: {} - triggering assault crime", witness->GetName());

        // Get the target's crime faction to determine assault bounty
        auto* crimeFaction = target->GetCrimeFaction();
        if (crimeFaction && crimeFaction->crimeData.crimevalues.assaultCrimeGold > 0) {
            std::uint16_t assaultBounty = crimeFaction->crimeData.crimevalues.assaultCrimeGold;

            SKSE::log::info("[WitnessDetection] Adding assault crime: faction={}, bounty={}",
                crimeFaction->GetName(), assaultBounty);

            // Add assault bounty to player (thread-safe: called from main update hook)
            player->ModCrimeGoldValue(crimeFaction, true, assaultBounty);

            // Send alarm to alert guards in the area - only when there's a valid crime faction
            // This prevents guards from attacking when feeding on followers/mercenaries who have no faction
            SendAssaultAlarm(target, player, true);

            // Log the new bounty
            std::uint32_t totalBounty = player->GetCrimeGoldValue(crimeFaction);
            SKSE::log::info("[WitnessDetection] Player bounty in {} is now {}", crimeFaction->GetName(), totalBounty);
        } else {
            SKSE::log::warn("[WitnessDetection] Target has no crime faction or assault bounty is 0 - no alarm sent, no bounty added");
        }

        // Don't force the victim into combat - they may be friendly, sleeping, etc.
        // The witness detection alone will alert guards naturally

        // Notify player
        auto message = fmt::format("You've been seen by {}!", witness->GetName());
        RE::DebugNotification(message.c_str());
    }
}
