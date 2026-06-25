#pragma once
#include "PCH.h"

namespace SoundUtil {
    // Default vampire feed sound, configured via Settings::Animation.FeedSoundForm
    // ("PluginName|0xFormID", default vanilla NPCHumanVampireFeed Skyrim.esm|0x0FF984).
    // Resolved lazily and cached per spec, so runtime INI edits are picked up.
    // GetFeedSound() returns the descriptor (or nullptr); PlayFeedSound() plays it 3D
    // at the actor. Used by the composite feed animation and by integrations that
    // don't supply their own feed sound.
    RE::BGSSoundDescriptorForm* GetFeedSound();
    void PlayFeedSound(RE::Actor* target);

    // Play the configured feed sound once at the player (UI preview button).
    void PlayFeedSoundTest();
}
