#pragma once
#include "RE/Skyrim.h"
#include <chrono>
#include <mutex>

class AnimEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
    static AnimEventSink* GetSingleton();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::BSAnimationGraphEvent* event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) override;

    static void Register();
    static void Unregister();
    static void CheckTimeout();

    // Also listen on a non-player actor (the composite feed target) so target-side
    // clip annotations like VFD_DrainedEnd are heard. Removed at feed teardown.
    static void AddToActor(RE::Actor* actor);
    static void RemoveFromActor(RE::Actor* actor);

private:
    AnimEventSink() = default;
    static std::chrono::steady_clock::time_point registeredTime_;
    static std::mutex mutex_;
};
