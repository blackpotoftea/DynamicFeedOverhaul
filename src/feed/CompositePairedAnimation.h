#pragma once

#include "RE/Skyrim.h"
#include "feed/AnimationRegistry.h"

// Multi-stage composite feed for STANDING non-combat targets. Plays two
// single-actor clips in sync per stage and sequences them:
//
//   Settle -> Intro(GoTo) -> Loop(Devour) -> Exit(GoBack) -> Drained -> Done
//                                \-> (drained dry in Loop) -> killed, Done
//
// Drain - and therefore death - happens ONLY in the Loop: every gulp removes
// HP, and when the victim hits the lethal floor it is killed in place (no
// GoBack/Drained on that path). If the player stops first, Exit plays the
// step-back and Drained plays the survivor's idle aftermath before teardown.
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

    // Event-driven stage advances: AnimEventSink calls these when a clip emits
    // its end annotation, so the next clip fires the instant the current one
    // finishes instead of waiting out a guessed timer (which leaves the actor in
    // default idle if it overruns the clip). Each is stage-guarded so a stray or
    // duplicate event is a no-op, and the matching Composite*Duration timer in
    // Tick() remains a fallback if the event never fires.
    //   VFD_GoToEnd / VFD_VampireFeedTrigger -> Intro -> Loop
    //   VFD_GoBackEnd                        -> Exit  -> Drained
    //   VFD_DrainedEnd                       -> Drained -> Done
    void OnIntroEnd();
    void OnExitEnd();
    void OnDrainedEnd();

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
