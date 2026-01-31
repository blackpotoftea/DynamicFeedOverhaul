#pragma once
#include "SkyPrompt/API.hpp"
#include "util.h"

class VampireFeedSink : public SkyPromptAPI::PromptSink {
public:
    static VampireFeedSink* GetSingleton() {
        static VampireFeedSink singleton;
        return &singleton;
    }

    std::span<const SkyPromptAPI::Prompt> GetPrompts() const override {
        return prompts_;
    }

    void ProcessEvent(SkyPromptAPI::PromptEvent event) const override {
        SKSE::log::debug("ProcessEvent called - type: {}", static_cast<int>(event.type));

        switch (event.type) {
        case SkyPromptAPI::PromptEventType::kAccepted:
            if (currentTarget_) {
                SKSE::log::info("Feed ACCEPTED on target: {} (FormID: {:X})",
                    currentTarget_->GetName(), currentTarget_->GetFormID());

                auto player = RE::PlayerCharacter::GetSingleton();
                if (!player) break;

                // Try native InitiateVampireFeedPackage first (may handle front/back automatically)
                SKSE::log::info("Calling InitiateVampireFeedPackage...");
                player->InitiateVampireFeedPackage(currentTarget_, nullptr);

                // Fallback: if native doesn't work, manually play idle
                // RE::TESIdleForm* feedIdle = RE::TESForm::LookupByEditorID<RE::TESIdleForm>("IdleVampireStandingFront");
                // if (feedIdle) {
                //     bool success = AnimUtil::Idle::Play(feedIdle, player, RE::DEFAULT_OBJECT::kActionIdle, currentTarget_);
                //     SKSE::log::info("Feed animation triggered: {}", success);
                // }
            }
            break;
        case SkyPromptAPI::PromptEventType::kDeclined:
            SKSE::log::debug("Feed DECLINED");
            break;
        case SkyPromptAPI::PromptEventType::kDown:
            SKSE::log::debug("Feed button DOWN");
            break;
        case SkyPromptAPI::PromptEventType::kUp:
            SKSE::log::debug("Feed button UP");
            break;
        default:
            break;
        }
    }

    void SetTarget(RE::Actor* target) {
        currentTarget_ = target;
        if (target) {
            prompts_[0] = SkyPromptAPI::Prompt(
                "Feed",
                1,  // eventID
                1,  // actionID
                SkyPromptAPI::PromptType::kSinglePress,
                target->GetFormID(),
                feedButtons_
            );
        }
    }

    RE::Actor* GetTarget() const {
        return currentTarget_;
    }

private:
    VampireFeedSink() {
        prompts_[0] = SkyPromptAPI::Prompt(
            "Feed",
            1,  // eventID
            1,  // actionID
            SkyPromptAPI::PromptType::kSinglePress,
            0,
            feedButtons_
        );
    }

    // G key (0x22) + Gamepad A (0x1000)
    static constexpr std::array<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>, 2> feedButtons_ = {{
        {RE::INPUT_DEVICE::kKeyboard, 0x22},
        {RE::INPUT_DEVICE::kGamepad, 0x1000}
    }};

    std::array<SkyPromptAPI::Prompt, 1> prompts_;
    mutable RE::Actor* currentTarget_ = nullptr;
};
