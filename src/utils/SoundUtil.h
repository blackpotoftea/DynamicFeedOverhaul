#pragma once
#include "PCH.h"

namespace SoundUtil {
    // Default vampire feed sound (vanilla NPCHumanVampireFeed, Skyrim.esm|0x0FF984),
    // resolved lazily and cached. GetFeedSound() returns the descriptor (or nullptr);
    // PlayFeedSound() plays it 3D at the actor. Used by the composite feed animation
    // and by integrations that don't supply their own feed sound.
    RE::BGSSoundDescriptorForm* GetFeedSound();
    void PlayFeedSound(RE::Actor* target);
}
