#pragma once
#include "SkyPrompt/API.hpp"
#include "TargetState.h"
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

                // Check target state
                auto sitSleepState = TargetState::GetSitSleepState(currentTarget_);
                bool isInFurniture = TargetState::IsInFurnitureState(currentTarget_);
                auto furnitureType = TargetState::GetActorFurnitureType(currentTarget_);
                auto furnitureRef = TargetState::GetFurnitureReference(currentTarget_);

                SKSE::log::info("Target state: {} | InFurniture: {} | FurnitureType: {}",
                    TargetState::SitSleepStateToString(sitSleepState),
                    isInFurniture,
                    TargetState::FurnitureTypeToString(furnitureType));

                if (TargetState::IsSleeping(currentTarget_)) {
                    SKSE::log::info("Target is SLEEPING - using bed feed");
                } else if (TargetState::IsSitting(currentTarget_)) {
                    SKSE::log::info("Target is SITTING - using seated feed");
                } else if (TargetState::IsStanding(currentTarget_)) {
                    SKSE::log::info("Target is STANDING - using standing feed");
                }

                // InitiateVampireFeedPackage takes furniture ref as second param for bed feeding
                SKSE::log::info("Calling InitiateVampireFeedPackage...");
                player->InitiateVampireFeedPackage(currentTarget_, furnitureRef);
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

    // Returns true if target should be excluded from feeding (no prompt shown)
    static bool IsExcluded(RE::Actor* actor) {
        if (!actor) return true;

        auto furnitureType = TargetState::GetActorFurnitureType(actor);

        // Exclude: sitting in chair (no vanilla feed animation for chairs)
        if (furnitureType == TargetState::FurnitureType::kChair) {
            SKSE::log::debug("Excluded: {} is sitting in chair", actor->GetName());
            return true;
        }

        return false;
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
