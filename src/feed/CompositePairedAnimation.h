#pragma once

#include "RE/Skyrim.h"
#include "feed/AnimationRegistry.h"

// Multi-stage composite feed for STANDING non-combat targets. Plays two
// single-actor clips in sync per stage and sequences them:
//
//   Settle -> Intro -> Loop -> Exit      (player presses Stop)
//                          \-> Killing    (victim drained to near-zero HP)
//
// Each stage fires a {player,target} clip pair (raw NotifyAnimationGraph
// events) and re-locks both actors at the scene offset. Transitions are driven
// by per-stage timer durations in Settings::NonCombat. The Loop is the
// exception: it plays indefinitely until the player Stops or the victim is
// drained to death.
namespace CompositePairedAnimation {

    // Begin the staged sequence on `target` using the supplied clip pack.
    bool Play(RE::Actor* target, const Feed::CompositePack& pack);

    // Player asked to stop feeding: transitions Settle/Intro/Loop -> Exit so
    // the release animation plays before teardown. No-op once Exit/Killing
    // has begun (guards a double Stop press).
    void RequestStop();

    // Idempotent teardown. Called by FeedAnimState::MarkFeedEnded(); must NOT
    // call MarkFeedEnded() itself (would recurse).
    void OnComplete();

    // Hard stop: full teardown plus FeedAnimState::MarkFeedEnded(). Used on
    // target loss/death and as an external abort.
    void ForceStop();

    // Active = a staged sequence is in progress.
    bool IsActive();

    // Current feed target (null if inactive).
    RE::NiPointer<RE::Actor> GetFeedTarget();

    // Per-frame driver. `delta` is the frame time in seconds (PlayerUpdateHook
    // a_delta). Drives the settle countdown, stage timers, HP-based auto-kill,
    // and target-validity checks. No-op when inactive.
    void Tick(float delta);
}
