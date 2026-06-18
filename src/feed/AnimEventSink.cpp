#include "PCH.h"
#include "feed/AnimEventSink.h"
#include "feed/FeedAnimState.h"
#include "feed/PairedAnimPromptSink.h"
#include "feed/CompositePairedAnimation.h"
#include "Settings.h"
#include "utils/AnimUtil.h"
#include <cmath>
#include <random>

std::chrono::steady_clock::time_point AnimEventSink::registeredTime_{};
std::mutex AnimEventSink::mutex_;

AnimEventSink* AnimEventSink::GetSingleton() {
    static AnimEventSink singleton;
    return &singleton;
}

RE::BSEventNotifyControl AnimEventSink::ProcessEvent(
    const RE::BSAnimationGraphEvent* event,
    [[maybe_unused]] RE::BSTEventSource<RE::BSAnimationGraphEvent>* source)
{
    if (!event || !event->tag.data()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto& tag = event->tag;

    // Composite staged feed owns its own lifecycle (driven by Tick() timers),
    // and its intro/loop/exit clips may emit PairEnd/IdleStop mid-sequence, so
    // ignore those here while a composite feed is active to avoid an early teardown.
    if (tag == "PairEnd" || tag == "IdleStop") {
        if (CompositePairedAnimation::IsActive()) {
            SKSE::log::debug("{} ignored - composite feed owns its lifecycle", tag.c_str());
            return RE::BSEventNotifyControl::kContinue;
        }
        SKSE::log::info("{} detected - marking feed ended", tag.c_str());

        {
            std::lock_guard<std::mutex> lock(mutex_);
            registeredTime_ = std::chrono::steady_clock::time_point{};
        }

        // Move to main thread to avoid race conditions on feedTargetHandle_
        SKSE::GetTaskInterface()->AddTask([]() {
            FeedAnimState::MarkFeedEnded();
            AnimEventSink::Unregister();
            SKSE::log::debug("Animation event sink unregistered (deferred)");
        });
    } else if (tag == "KillMoveStart") {
        // Ground truth that the paired animation actually started in the engine.
        // AnimUtil's retry tick consumes this flag to confirm success and stop retrying.
        FeedAnimState::MarkKillMoveStartSeen();
    } else if (tag == "VFD_VampireFeedTrigger") {
        auto* settings = Settings::GetSingleton();
        if (settings->HealthDrain.Enable) {
            uint32_t triggerIdx = FeedAnimState::IncrementVFDTriggerCount();
            bool lethal = FeedAnimState::IsCurrentFeedLethal();

            float percent;
            if (lethal) {
                float minP = settings->HealthDrain.LethalChunkMinPercent;
                float maxP = settings->HealthDrain.LethalChunkMaxPercent;
                if (maxP < minP) std::swap(minP, maxP);

                thread_local std::random_device rd;
                thread_local std::mt19937 gen(rd());
                std::uniform_real_distribution<float> dist(minP, maxP);
                float roll = dist(gen);

                float escalation = std::pow(settings->HealthDrain.EscalationPerTrigger,
                                            static_cast<float>(triggerIdx - 1));
                percent = std::min(roll * escalation, settings->HealthDrain.MaxChunkCapPercent);
            } else {
                percent = settings->HealthDrain.NonLethalChunkPercent;
            }

            // Target drain honors FloorTargetAtOneHP — when off, drain can take the NPC to 0.
            bool targetFloor = settings->HealthDrain.FloorTargetAtOneHP;
            SKSE::log::debug("VFD_VampireFeedTrigger #{} (lethal={}, percent={:.1f}, targetFloor={})",
                             triggerIdx, lethal, percent, targetFloor);

            if (settings->HealthDrain.DrainOnNPC) {
                auto target = PairedAnimPromptSink::GetSingleton()->GetActiveFeedTarget();
                if (target) {
                    AnimUtil::DrainHealthChunk(target.get(), percent, targetFloor);
                } else {
                    SKSE::log::debug("VFD_VampireFeedTrigger: DrainOnNPC enabled but no active feed target");
                }
            }
        }
    } else {
         // Log all events during feed to discover weapon-related events
        //  SKSE::log::debug("[AnimEvent] {}", tag.c_str());
    }

    return RE::BSEventNotifyControl::kContinue;
}

void AnimEventSink::Register() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Remove first to prevent double registration
        player->RemoveAnimationGraphEventSink(GetSingleton());
        player->AddAnimationGraphEventSink(GetSingleton());
        registeredTime_ = std::chrono::steady_clock::now();
        SKSE::log::info("Animation event sink registered");
    }
}

void AnimEventSink::Unregister() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        // Safe to call even if not registered (idempotent)
        player->RemoveAnimationGraphEventSink(GetSingleton());
        SKSE::log::debug("Animation event sink unregistered");
    }
}

void AnimEventSink::CheckTimeout() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (registeredTime_.time_since_epoch().count() == 0) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - registeredTime_).count();

    float timeout = Settings::GetSingleton()->General.AnimationTimeout;

    if (elapsed >= timeout) {
        SKSE::log::warn("Animation event timeout ({:.1f}s) - forcing prompt refresh", timeout);
        registeredTime_ = std::chrono::steady_clock::time_point{}; // Reset
        lock.unlock(); // Release lock before calling external functions that might call back or take time

        // Move to main thread to avoid race conditions on feedTargetHandle_
        SKSE::GetTaskInterface()->AddTask([]() {
            FeedAnimState::MarkFeedEnded();
            AnimEventSink::Unregister();
        });
    }
}
