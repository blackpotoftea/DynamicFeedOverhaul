#pragma once

// Papyrus-facing natives so other mods can detect this SKSE-only plugin.
// There is no .esp/.esl to probe with Game.GetModByName, so presence is
// exposed through registered globals on the DynamicFeedOverhaul script.
namespace PapyrusNatives {

    // The DLL being loaded is what makes this return true. If the DLL is
    // absent the native stays unregistered and the VM call returns the
    // default (false) - that is the detection signal.
    inline bool IsInstalled(RE::StaticFunctionTag*) { return true; }

    // Bump on API changes so scripts can gate on capability, not just presence.
    inline std::int32_t GetVersion(RE::StaticFunctionTag*) { return 1; }

    inline bool Register(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("IsInstalled", "DynamicFeedOverhaul", IsInstalled);
        vm->RegisterFunction("GetVersion", "DynamicFeedOverhaul", GetVersion);
        SKSE::log::info("Registered DynamicFeedOverhaul Papyrus natives");
        return true;
    }

}  // namespace PapyrusNatives
