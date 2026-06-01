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

private:
    AnimEventSink() = default;
    static std::chrono::steady_clock::time_point registeredTime_;
    static std::mutex mutex_;
};
