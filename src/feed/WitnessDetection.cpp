#include "WitnessDetection.h"
#include "Settings.h"
#include "TargetState.h"
#include "papyrus/PapyrusCall.h"

namespace {
    // Custom relocation for SendAssaultAlarm function not exposed in CommonLibSSE
    // 1405DE810       1406042C0
    // Monitor221hz
    void SendAssaultAlarm(RE::Actor* a_victim, RE::Actor* a_assaulter, bool arg3) {
        using func_t = decltype(SendAssaultAlarm);
        REL::Relocation<func_t> func{ RELOCATION_ID(36429, 37424) };
        func(a_victim, a_assaulter, arg3);
    }

    // How a witness reacts to seeing/experiencing the feed:
    //   kIgnore - friendly to the player: no attack, no crime report
    //   kReport - registers the assault crime (bounty) but does not attack
    //   kAttack - turns hostile and fights the player (also implies a report)
    enum class WitnessReaction { kIgnore, kReport, kAttack };

    // 3-tier witness model (friendly ignores, hostile attacks, neutral decides by
    // Confidence). When relationship-awareness is off, fall back to a pure Confidence
    // decision (report vs attack) and never ignore. The relationship/disposition and
    // Confidence queries are general actor facts and live in TargetState; this stays
    // here as the witness policy that consults settings.
    WitnessReaction ClassifyWitness(RE::Actor* witness, RE::Actor* player) {
        if (!witness || !player) return WitnessReaction::kIgnore;
        auto* settings = Settings::GetSingleton();
        const int conf = static_cast<int>(TargetState::GetConfidence(witness));
        const bool brave = conf >= settings->Combat.AssaultConfidenceThreshold;

        if (!settings->Combat.WitnessRelationshipAware) {
            return brave ? WitnessReaction::kAttack : WitnessReaction::kReport;
        }

        switch (TargetState::GetDisposition(witness, player)) {
        case TargetState::Disposition::Friendly: return WitnessReaction::kIgnore;
        case TargetState::Disposition::Hostile:  return WitnessReaction::kAttack;
        default:                                 return brave ? WitnessReaction::kAttack : WitnessReaction::kReport;
        }
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

        // RequestDetectionLevel returns a positive integer; values >= ~50 indicate the
        // actor is actively aware of the target. Values can far exceed 100 when fully
        // alerted in combat.
        std::int32_t detectionLevel = GetDetectionLevel(potentialWitness, player);

        if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] {} detection level: {} (threshold: 50)",
                potentialWitness->GetName(), detectionLevel);
        }

        if (detectionLevel >= 50) {
            if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] Actor {} has DETECTED player (level: {})",
                    potentialWitness->GetName(), detectionLevel);
            }
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

            // Check if this actor can witness the feed and isn't friendly enough to let it slide.
            if (CanActorWitnessFeed(actor, player, target) &&
                ClassifyWitness(actor, player) != WitnessReaction::kIgnore) {
                SKSE::log::trace("[WitnessDetection] Feed witnessed by: {} (distance: {:.1f})",
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

        // Hostile targets cannot be the victim of an assault crime — killing an enemy in
        // active combat is not reportable, and any nearby hostile NPCs are threats, not
        // witnesses. Short-circuit to avoid scanning and log spam every WitnessCheckInterval.
        if (target->IsHostileToActor(player)) {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] target is hostile - no crime possible, skipping");
            }
            return;
        }

        if (settings->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] checking for witnesses (player: {}, target: {})",
                player->GetName(), target->GetName());
        }

        // Check if the victim themselves should raise alarm (if awake and not a follower).
        // A friendly victim (kIgnore) won't report you and must NOT short-circuit the
        // scan below - a nearby non-friendly NPC can still see it and report.
        if (!target->IsPlayerTeammate() && TargetState::IsConsciousAndAware(target)) {
            if (ClassifyWitness(target, player) != WitnessReaction::kIgnore) {
                if (settings->Combat.WitnessDebugLogging) {
                    SKSE::log::debug("[WitnessDetection] Victim {} is conscious and not a teammate - raising alarm",
                        target->GetName());
                }
                OnDetectedByWitness(player, target, target);  // Victim is their own witness
                return;  // Victim reported; no need to scan for other witnesses
            }
            // Friendly aware victim: skip self-report, fall through to the bystander scan.
        }

        // Use the WitnessDetection module to check for witnesses
        RE::Actor* witness = CheckForWitnesses(player, target);
        if (witness) {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] found witness {}", witness->GetName());
            }
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
        SKSE::log::trace("[WitnessDetection] Feed witnessed by: {} - triggering assault crime", witness->GetName());

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

    void ApplyWitnessReactions(RE::Actor* player, RE::Actor* victim) {
        auto* settings = Settings::GetSingleton();
        if (!settings->Combat.EnableWitnessCombatReaction || !player) return;

        // Start combat on a witness, deferred one frame so the feed teardown's
        // restraint release / AI refresh has settled (else StartCombat can fizzle
        // against a still-restrained actor).
        auto startCombatDeferred = [](RE::Actor* w) {
            auto handle = w->CreateRefHandle();
            SKSE::GetTaskInterface()->AddTask([handle] {
                auto ref = handle.get();
                auto* witness = ref ? ref->As<RE::Actor>() : nullptr;
                if (!witness || witness->IsDead()) return;
                auto* pl = RE::PlayerCharacter::GetSingleton();
                if (!pl) return;
                SKSE::log::info("[WitnessDetection] {} turns hostile after witnessing the feed", witness->GetName());
                PapyrusCall::StartCombat(witness, pl);
            });
        };

        // 1. The victim itself - an awake, surviving, non-follower victim witnessed
        //    its own assault. Attack only if the 3-tier model says so.
        if (victim && !victim->IsDead() && !victim->IsDisabled() &&
            !victim->IsPlayerTeammate() && TargetState::IsConsciousAndAware(victim) &&
            ClassifyWitness(victim, player) == WitnessReaction::kAttack) {
            startCombatDeferred(victim);
        }

        // 2. Bystanders who detected the feed (LOS + detection), excluding the victim.
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;
        const auto playerPos = player->GetPosition();
        const float radius = settings->Combat.WitnessDetectionRadius;
        for (auto& actorHandle : processLists->highActorHandles) {
            auto actorPtr = actorHandle.get();
            if (!actorPtr) continue;
            auto* actor = actorPtr.get();
            if (!actor || actor == victim) continue;
            if (actor->GetPosition().GetDistance(playerPos) > radius) continue;
            if (!CanActorWitnessFeed(actor, player, victim)) continue;
            if (ClassifyWitness(actor, player) == WitnessReaction::kAttack) {
                startCombatDeferred(actor);
            }
        }
    }
}
