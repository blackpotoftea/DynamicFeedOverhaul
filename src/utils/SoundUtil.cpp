#include "utils/SoundUtil.h"
#include "integration/VampireIntegrationUtils.h"

namespace SoundUtil {

    RE::BGSSoundDescriptorForm* GetFeedSound() {
        static RE::BGSSoundDescriptorForm* s_cached = nullptr;
        static bool s_tried = false;
        if (!s_tried) {
            s_tried = true;
            if (auto* dh = RE::TESDataHandler::GetSingleton()) {
                s_cached = dh->LookupForm<RE::BGSSoundDescriptorForm>(0x0FF984, "Skyrim.esm");
            }
            if (!s_cached) {
                SKSE::log::warn("SoundUtil::GetFeedSound: feed sound (Skyrim.esm|0x0FF984) not found");
            }
        }
        return s_cached;
    }

    void PlayFeedSound(RE::Actor* target) {
        VampireIntegrationUtils::PlaySound(GetFeedSound(), target);  // PlaySound null-guards both args
    }
}
