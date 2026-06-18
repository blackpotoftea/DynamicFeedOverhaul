#include "feed/CombatBark.h"

#include <mutex>
#include <unordered_map>

namespace {
    using Subtype = RE::DIALOGUE_DATA::Subtype;

    Subtype ToSubtype(CombatBark::Type t) {
        switch (t) {
            case CombatBark::Type::Hit:         return Subtype::kHit;
            case CombatBark::Type::PowerAttack: return Subtype::kPowerAttack;
            case CombatBark::Type::Bleedout:    return Subtype::kBleedout;
            case CombatBark::Type::Death:       return Subtype::kDeath;
        }
        return Subtype::kHit;
    }

    // Resolve and cache the canonical generic combat topic for a subtype.
    //
    // The DialogueGeneric > Combat shared topics live in the base game
    // (Skyrim.esm) and carry every voice-type recording, so the engine can
    // voice-match any humanoid speaker. Mod-added topics of the same subtype
    // usually cover only one custom voice type and would play nothing for a
    // vanilla NPC, so we deliberately prefer the base-game topic with the most
    // recorded infos. Resolution is lazy + cached so a stable subtype only
    // walks the form array once.
    RE::TESTopic* ResolveTopic(Subtype sub) {
        static std::mutex s_mutex;
        static std::unordered_map<std::uint16_t, RE::TESTopic*> s_cache;

        const auto key = static_cast<std::uint16_t>(sub);

        std::lock_guard lock(s_mutex);
        if (auto it = s_cache.find(key); it != s_cache.end()) {
            return it->second;
        }

        RE::TESTopic* best = nullptr;
        // Score: base-game (Skyrim.esm) topics rank above any mod topic, and
        // within a tier the one with the most infos (= most voice coverage)
        // wins. Encode as (isBaseGame ? 1 : 0) << 20 | numInfos.
        std::uint32_t bestScore = 0;

        if (auto* dh = RE::TESDataHandler::GetSingleton()) {
            for (auto* topic : dh->GetFormArray<RE::TESTopic>()) {
                if (!topic || topic->data.subtype.get() != sub) continue;
                if (topic->numTopicInfos == 0) continue;

                const bool isBaseGame = (topic->GetFormID() >> 24) == 0x00;  // Skyrim.esm
                const std::uint32_t score =
                    (isBaseGame ? (1u << 20) : 0u) | std::min<std::uint32_t>(topic->numTopicInfos, 0xFFFFu);

                if (score > bestScore) {
                    bestScore = score;
                    best = topic;
                }
            }
        }

        s_cache[key] = best;
        if (!best) {
            SKSE::log::warn("[CombatBark] No combat topic found for subtype {}", key);
        } else {
            const auto* quest = best->ownerQuest;
            SKSE::log::info("[CombatBark] Resolved subtype {} -> topic {:08X} ({} infos, ownerQuest {:08X})",
                            key, best->GetFormID(), best->numTopicInfos,
                            quest ? quest->GetFormID() : 0u);
        }
        return best;
    }
}

namespace CombatBark {
    void Play(RE::Actor* actor, Type type) {
        if (!actor) return;

        auto* topic = ResolveTopic(ToSubtype(type));
        if (!topic) return;

        const auto actorHandle = actor->CreateRefHandle();
        const auto topicID = topic->GetFormID();

        SKSE::GetTaskInterface()->AddTask([actorHandle, topicID]() {
            auto ref = actorHandle.get();
            if (!ref) return;
            auto* a = ref->As<RE::Actor>();
            // Caller controls timing relative to death; we only require loaded
            // 3D so the engine has a speaker to attach the voice to.
            if (!a || !a->Is3DLoaded()) return;

            auto* topic = RE::TESForm::LookupByID<RE::TESTopic>(topicID);
            if (!topic) return;

            // Diagnostics: which voice type the engine will try to match, and
            // whether the actor is mid voice-cooldown (a positive recovery
            // time means a freshly-played line will be suppressed).
            if (auto* base = a->GetActorBase()) {
                auto* voice = base->GetVoiceType();
                SKSE::log::info("[CombatBark] Say on {} (voiceType {}, recovery {:.2f}s)",
                                a->GetName(),
                                voice ? voice->GetFormEditorID() : "<none>",
                                a->GetVoiceRecoveryTime());
            }

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return;

            auto* policy = vm->GetObjectHandlePolicy();
            auto vmHandle = policy->GetHandleForObject(RE::Actor::FORMTYPE, a);
            if (vmHandle == policy->EmptyHandle()) return;

            // ObjectReference.Say(Topic akTopicToSay, ActorBase akActorToSpeakAs = None,
            //                     Bool abSpeakInPlayersHead = false,
            //                     ObjectReference akTargetToSpeakTo = None)
            RE::TESTopic*       topicArg        = topic;
            RE::TESNPC*         speakerOverride = nullptr;  // None -> speak as self
            bool                inPlayersHead   = false;
            RE::TESObjectREFR*  speakTarget     = nullptr;  // None
            auto* args = RE::MakeFunctionArguments(std::move(topicArg), std::move(speakerOverride),
                                                   std::move(inPlayersHead), std::move(speakTarget));

            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            if (!vm->DispatchMethodCall(vmHandle, "ObjectReference", "Say", args, callback)) {
                // DispatchMethodCall takes ownership only on success.
                delete args;
                SKSE::log::warn("[CombatBark] Say dispatch failed for {} (topic {:08X})",
                                a->GetName(), topicID);
            }
        });
    }
}
