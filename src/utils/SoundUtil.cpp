#include "utils/SoundUtil.h"
#include "Settings.h"
#include "integration/VampireIntegrationUtils.h"

namespace SoundUtil {

    RE::BGSSoundDescriptorForm* GetFeedSound() {
        // Resolve from the configurable INI spec, cached per spec so runtime edits
        // pick up the new value but a stable string only hits the data handler once.
        const std::string& spec = Settings::GetSingleton()->Animation.FeedSoundForm;
        if (spec.empty()) return nullptr;

        static std::string s_cachedSpec;
        static RE::BGSSoundDescriptorForm* s_cachedSound = nullptr;
        if (spec == s_cachedSpec) return s_cachedSound;

        s_cachedSpec = spec;
        s_cachedSound = nullptr;

        auto delim = spec.find('|');
        if (delim == std::string::npos) {
            SKSE::log::warn("SoundUtil::GetFeedSound: FeedSoundForm missing '|': '{}'", spec);
            return nullptr;
        }
        std::string plugin = spec.substr(0, delim);
        std::string idStr = spec.substr(delim + 1);
        try {
            RE::FormID id = static_cast<RE::FormID>(std::stoul(idStr, nullptr, 16));
            if (auto* dh = RE::TESDataHandler::GetSingleton()) {
                s_cachedSound = dh->LookupForm<RE::BGSSoundDescriptorForm>(id, plugin);
            }
        } catch (...) {
            SKSE::log::warn("SoundUtil::GetFeedSound: FeedSoundForm bad FormID hex: '{}'", idStr);
            return nullptr;
        }
        if (!s_cachedSound) {
            SKSE::log::warn("SoundUtil::GetFeedSound: FeedSoundForm not found: '{}'", spec);
        }
        return s_cachedSound;
    }

    void PlayFeedSound(RE::Actor* target) {
        VampireIntegrationUtils::PlaySound(GetFeedSound(), target);  // PlaySound null-guards both args
    }

    void PlayFeedSoundTest() {
        auto* sound = GetFeedSound();
        if (!sound) {
            SKSE::log::warn("SoundUtil::PlayFeedSoundTest: no resolvable sound (check FeedSoundForm)");
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::warn("SoundUtil::PlayFeedSoundTest: player not available");
            return;
        }
        SKSE::log::info("SoundUtil::PlayFeedSoundTest: test-playing feed sound (FormID {:X}) at player", sound->GetFormID());
        VampireIntegrationUtils::PlaySound(sound, player);
    }
}
