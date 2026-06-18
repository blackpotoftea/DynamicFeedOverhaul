#pragma once

#include "RE/Skyrim.h"

// Generic combat vocalizations ("barks"): pain grunts, death screams, etc.
//
// In Skyrim these are NOT sound descriptors - they are dialogue responses
// stored under the DialogueGeneric quest's Combat branch (subtypes Hit,
// Death, Bleedout, PowerAttack...). Each response is recorded once per
// voice type. Rather than enumerate sounds and match the actor ourselves,
// we make the actor *Say* the matching combat topic; the engine resolves
// the voice-type-correct recording automatically. This avoids real damage
// (no blood decals, no flinch, no AI aggro) while producing the exact
// vocalization the actor would make when hurt.
//
// =====================================================================
// STATUS: TABLED - currently produces no audio. See TODO below.
// =====================================================================
// Investigation (2026-06-18):
//   * Topic resolution works: subtype kHit -> 00013EBB (DialogueGeneric,
//     30 infos), voice type read correctly (e.g. FemaleDarkElf), voice
//     recovery 0.00s, and DispatchMethodCall("ObjectReference","Say")
//     returns success.
//   * Yet nothing plays, even with abSpeakInPlayersHead = true (2D).
//   * NOT an actor-state/animation suppression issue: SkyrimNet (TTS) makes
//     the same NPC talk + lip-sync DURING our feed animation, so the engine
//     voice path is open.
//   * Root cause: these are *Combat*-type topic infos with conditions that
//     require the speaker to be in combat. Our feed target is pacified, so
//     Say evaluates the conditions, finds no eligible info, and stays silent.
//
// TODO(bark): pick an approach to actually emit the bark, then re-enable the
// call site in CompositePairedAnimation::FireDeferredPlay:
//   1. Force-play the info, bypassing conditions: build a DialogueItem
//      (DialogueItem ctor RELOCATION_ID 34413/35220, or
//      TESTopicInfo::GetDialogueData(speaker)) and drive it via
//      Actor::UpdateInDialogue. Authentic voice+lip; highest effort/CTD risk.
//   2. Satisfy the condition: briefly flag the target in-combat around Say,
//      then restore. Low effort; fights pacify; only if condition is fakeable.
//   3. Curate our own pain/scream BGSSoundDescriptorForms and play them via
//      VampireIntegrationUtils::PlaySound, matched by sex (+voice if named).
//      Guaranteed sound, low risk; needs assets and isn't the exact vanilla line.
//   4. Find a non-condition-gated bark info and Say that instead (uncertain).
// Next cheap step: log TESTopicInfo::objConditions to confirm the gate, which
// decides between option 2 (fakeable) and option 3 (guaranteed fallback).
namespace CombatBark {
    enum class Type {
        Hit,          // kHit (29)         - short pain grunt on taking a hit
        PowerAttack,  // kPowerAttack (27) - heavier grunt
        Bleedout,     // kBleedout (31)    - low/wounded vocalization
        Death,        // kDeath (33)       - death scream
    };

    // Make the actor emit the generic combat vocalization matching its voice
    // type. No-op if the actor, its voice type, or the topic can't be
    // resolved. Safe to call from any thread (defers to the game thread).
    void Play(RE::Actor* actor, Type type);
}
