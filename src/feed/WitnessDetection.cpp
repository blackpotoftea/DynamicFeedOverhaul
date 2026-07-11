#include "WitnessDetection.h"
#include "Settings.h"
#include "TargetState.h"
#include "CompositePairedAnimation.h"
#include "papyrus/PapyrusCall.h"
#include "utils/FormUtils.h"

#include <atomic>

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

    // Latched true once a feed has been reported to guards, so a single feed produces a
    // single assault charge no matter how many witness-check ticks see it. Reset at the
    // start of each feed via WitnessDetection::ResetFeedReport(). Atomic because the reset
    // runs on the feed-start (prompt-accept) path while reads/sets run on the update hook -
    // the same cross-path access FeedAnimState guards with atomics.
    std::atomic<bool> g_feedReported{ false };

    // Feeding on this actor is a legal feed: no bounty, no alarm, no combat - for anyone. Vampire's
    // Seduction/Mesmerize adds its target to DLC1VampireFeedNoCrimeFaction (see DLC1VampireMesmerizeScript),
    // so this faction list also covers charmed victims without a separate magic-effect check.
    bool IsFeedCrimeExempt(RE::Actor* actor) {
        return FormUtils::IsInAnyFaction(actor, Settings::GetSingleton()->Combat.NoCrimeFeedFactions);
    }

    // This actor personally never reports/attacks a feed (as witness or victim), e.g. the player's
    // own thralls - but a non-member witness can still report a feed on it.
    bool IsIgnoredWitness(RE::Actor* actor) {
        return FormUtils::IsInAnyFaction(actor, Settings::GetSingleton()->Combat.IgnoreWitnessFactions);
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

        // Fellow vampires/Vampire Lords, and members of the no-crime / ignore-witness factions
        // (e.g. the player's own thralls), never report or fight over a feed they witness.
        const bool vampKin = Settings::GetSingleton()->Combat.WitnessIgnoreVampires &&
            (TargetState::IsVampire(potentialWitness) || TargetState::IsVampireLord(potentialWitness));
        if (vampKin || IsFeedCrimeExempt(potentialWitness) || IsIgnoredWitness(potentialWitness)) {
            if (Settings::GetSingleton()->Combat.WitnessDebugLogging) {
                SKSE::log::trace("[WitnessDetection] {} is vampire-kin/exempt-faction, skipping", potentialWitness->GetName());
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

        // Check if actor can see the player or target (HasLineOfSight requires reference parameter).
        // LOS is the expensive part of the per-tick scan (a raycast per actor); we only need ONE
        // of player/target visible, so skip the target raycast when the player is already visible.
        // Once the player is freed (Drained tail) he looks innocent - only the still-drained
        // victim betrays the feed, so the player raycast no longer counts.
        const bool playerReleased = CompositePairedAnimation::IsPlayerReleased();
        bool losResult1 = false;
        bool losResult2 = false;
        bool canSeePlayer = !playerReleased && potentialWitness->HasLineOfSight(player, losResult1);
        bool canSeeTarget = false;
        if (!canSeePlayer) {
            canSeeTarget = potentialWitness->HasLineOfSight(target, losResult2);
        }

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

        // After release the scene is the drained victim, not the freed player.
        auto scenePos = CompositePairedAnimation::IsPlayerReleased()
            ? target->GetPosition() : player->GetPosition();
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
            float distance = actor->GetPosition().GetDistance(scenePos);
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

    // Start combat on an actor, deferred one frame via the task graph. Deferral keeps the
    // StartCombat (an AI mutation) off the actor-update path we're called from, and lets a
    // just-released victim's AI refresh settle (else StartCombat can fizzle on a still-
    // restrained actor). Re-resolves the actor from a handle inside the task - never holds a
    // raw pointer across the frame boundary.
    static void StartCombatDeferred(RE::Actor* a_actor) {
        if (!a_actor) return;
        auto handle = a_actor->CreateRefHandle();
        SKSE::GetTaskInterface()->AddTask([handle] {
            auto ref = handle.get();
            auto* witness = ref ? ref->As<RE::Actor>() : nullptr;
            if (!witness || witness->IsDead()) return;
            auto* pl = RE::PlayerCharacter::GetSingleton();
            if (!pl) return;
            SKSE::log::info("[WitnessDetection] {} turns hostile after witnessing the feed", witness->GetName());
            PapyrusCall::StartCombat(witness, pl);
        });
    }

    // Bystanders (everyone but the victim) who can see the feed and are hostile enough turn
    // hostile the instant they notice - they are free actors, so unlike the restrained victim
    // they react live during the feed. Called every tick; the IsInCombat() guard makes each
    // bystander engage exactly once. Gated by EnableWitnessCombatReaction.
    static void TriggerBystanderCombat(RE::Actor* player, RE::Actor* victim) {
        auto* settings = Settings::GetSingleton();
        if (!settings->Combat.EnableWitnessCombatReaction || !player) return;

        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;
        // After release the scene is the drained victim, not the freed player.
        const auto scenePos = (victim && CompositePairedAnimation::IsPlayerReleased())
            ? victim->GetPosition() : player->GetPosition();
        const float radius = settings->Combat.WitnessDetectionRadius;
        for (auto& actorHandle : processLists->highActorHandles) {
            auto actorPtr = actorHandle.get();
            if (!actorPtr) continue;
            auto* actor = actorPtr.get();
            if (!actor || actor == victim) continue;
            if (actor->IsInCombat()) continue;  // already engaged - don't restart every tick
            if (actor->GetPosition().GetDistance(scenePos) > radius) continue;
            if (!CanActorWitnessFeed(actor, player, victim)) continue;
            if (ClassifyWitness(actor, player) == WitnessReaction::kAttack) {
                StartCombatDeferred(actor);
            }
        }
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

        // Abandon witnessing once the scene falls apart: victim unloaded, or player
        // and victim in different cells (coc/teleport mid-feed). Cells may differ
        // legitimately across an exterior border, so also require out-of-radius.
        if (!target->Is3DLoaded() ||
            (player->GetParentCell() != target->GetParentCell() &&
             target->GetPosition().GetDistance(player->GetPosition()) > settings->Combat.WitnessDetectionRadius)) {
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

        // Victim in a no-crime feed faction (incl. anyone under Vampire's Seduction, which adds
        // DLC1VampireFeedNoCrimeFaction): a legal feed. Suppress bystander combat, victim self-report,
        // and the bounty together. No latch - re-checking is cheap and resumes if the flag clears.
        if (IsFeedCrimeExempt(target)) {
            if (settings->Combat.WitnessDebugLogging) {
                SKSE::log::debug("[WitnessDetection] target is in a no-crime feed faction - feed is legal, skipping");
            }
            return;
        }

        // Bystander combat reactions: evaluated live every tick so each hostile witness engages
        // the moment it notices the feed. Independent of the one-shot bounty below (and of the
        // victim, which is restrained until release - handled at feed end in ApplyWitnessReactions).
        TriggerBystanderCombat(player, target);

        // Bounty/alarm is settled once per feed; skip re-charging once done. (Bystander combat
        // above is intentionally NOT gated by this - it keeps reacting to newcomers.)
        if (g_feedReported) {
            return;
        }

        if (settings->Combat.WitnessDebugLogging) {
            SKSE::log::debug("[WitnessDetection] checking for witnesses (player: {}, target: {})",
                player->GetName(), target->GetName());
        }

        // Check if the victim themselves should raise alarm (if awake and not a follower).
        // A friendly victim (kIgnore) won't report you and must NOT short-circuit the
        // scan below - a nearby non-friendly NPC can still see it and report. An ignore-witness
        // victim (e.g. a thrall) never self-reports, but is left in the scan so a non-member guard can.
        if (!target->IsPlayerTeammate() && !IsIgnoredWitness(target) && TargetState::IsConsciousAndAware(target)) {
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

        // One outcome per feed. The witness check runs every WitnessCheckInterval for the
        // whole feed; without this latch each tick re-ran this - re-adding the bounty, or
        // (for faction-less victims) re-logging and re-notifying the player every tick.
        if (g_feedReported) {
            SKSE::log::debug("[WitnessDetection] Feed already resolved - skipping");
            return;
        }
        // This call settles the feed's witness outcome (the bounty is assessed against the
        // victim's crime faction, which is fixed for the whole feed). Latch now so the
        // per-tick check stops, whether or not a crime ends up applying. Reset at feed start.
        g_feedReported = true;

        // Faction-less victims (a generic Courier, summons, some mercenaries) have no
        // jurisdiction to report to - feeding on them is not a reportable crime. Settle
        // silently: no bounty, no alarm, no "you've been seen" notification.
        auto* crimeFaction = target->GetCrimeFaction();
        if (!crimeFaction || crimeFaction->crimeData.crimevalues.assaultCrimeGold == 0) {
            SKSE::log::info("[WitnessDetection] {} has no crime faction - feed not a reportable crime",
                target->GetName());
            return;
        }

        SKSE::log::trace("[WitnessDetection] Feed witnessed by: {} - triggering assault crime", witness->GetName());

        // Bounty size: use the configured override when set, otherwise the hold's own
        // vanilla assault crime gold.
        const int configured = Settings::GetSingleton()->Combat.WitnessAssaultBounty;
        const std::int32_t assaultBounty = configured > 0
            ? configured
            : static_cast<std::int32_t>(crimeFaction->crimeData.crimevalues.assaultCrimeGold);

        SKSE::log::info("[WitnessDetection] Adding assault crime: faction={}, bounty={}",
            crimeFaction->GetName(), assaultBounty);

        // Add assault bounty to player (thread-safe: called from main update hook)
        player->ModCrimeGoldValue(crimeFaction, true, assaultBounty);

        // Send alarm to alert guards in the area.
        SendAssaultAlarm(target, player, true);

        const std::uint32_t totalBounty = player->GetCrimeGoldValue(crimeFaction);
        SKSE::log::info("[WitnessDetection] Player bounty in {} is now {}", crimeFaction->GetName(), totalBounty);

        // Notify the player only when the feed actually became a crime.
        auto message = fmt::format("You've been seen by {}!", witness->GetName());
        RE::DebugNotification(message.c_str());
    }

    void ResetFeedReport() {
        g_feedReported = false;
    }

    bool IsFeedReported() {
        return g_feedReported;
    }

    void ApplyWitnessReactions(RE::Actor* player, RE::Actor* victim) {
        auto* settings = Settings::GetSingleton();
        if (!settings->Combat.EnableWitnessCombatReaction || !player) return;

        // A no-crime-faction victim (legal feed) or an ignore-witness victim (e.g. a thrall) never
        // fights back at release.
        if (IsFeedCrimeExempt(victim) || IsIgnoredWitness(victim)) return;

        // Victim-only. Bystanders already turned hostile live during the feed (see
        // TriggerBystanderCombat in PerformWitnessCheck). The victim can only react here, at
        // feed end, because it was restrained in the paired animation until teardown released
        // it. Attack only if the awake, surviving, non-follower victim's 3-tier model says so.
        if (victim && !victim->IsDead() && !victim->IsDisabled() &&
            !victim->IsPlayerTeammate() && TargetState::IsConsciousAndAware(victim) &&
            ClassifyWitness(victim, player) == WitnessReaction::kAttack) {
            StartCombatDeferred(victim);
        }
    }
}
