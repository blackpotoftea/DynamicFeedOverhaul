#include "PCH.h"
#include "feed/AnimationRegistry.h"
#include "feed/TargetState.h"
#include "feed/PairedAnimation.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Feed {

    AnimationRegistry* AnimationRegistry::GetSingleton() {
        static AnimationRegistry singleton;
        return &singleton;
    }

    void AnimationRegistry::Clear() {
        animations_.clear();
        compositePacks_.clear();
    }

    // Helper to parse enums from string
    Direction ParseDirection(const std::string& str) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "back") return Direction::Back;
        if (s == "any") return Direction::Any;
        if (s == "left") return Direction::Left;
        if (s == "right") return Direction::Right;
        return Direction::Front;
    }

    Sex ParseSex(const std::string& str) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "female") return Sex::Female;
        if (s == "male") return Sex::Male;
        return Sex::Unisex;
    }

    Type ParseType(const std::string& str) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "combat") return Type::Combat;
        return Type::Normal;
    }

    Furniture ParseFurniture(const std::string& str) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "bed") return Furniture::Bed;
        if (s == "bedroll") return Furniture::Bedroll;
        return Furniture::None;
    }

    void AnimationRegistry::LoadAnimations(const std::string& directoryPath) {
        if (!fs::exists(directoryPath)) {
            SKSE::log::warn("Animation directory not found: {}", directoryPath);
            return;
        }

        SKSE::log::info("Loading animations from: {}", directoryPath);

        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file()) {
                // path::string() throws on filenames not representable in the ANSI
                // code page, so match the suffix on the native wide name instead.
                const std::wstring wname = entry.path().filename().native();
                // Load every *_DFO.json file in the folder so other mods can drop in
                // their own packs to extend the system. Each entry is a regular
                // animation definition or a staged composite pack (decided by the
                // "stages" key below); the mod ships main_DFO.json + main_c_DFO.json.
                if (wname.length() >= 9 &&
                    wname.compare(wname.length() - 9, 9, L"_DFO.json") == 0) {

                    const auto u8name = entry.path().filename().u8string();
                    const std::string filename(reinterpret_cast<const char*>(u8name.data()), u8name.size());
                    try {
                        std::ifstream i(entry.path());
                        json j;
                        i >> j;

                        for (auto& [key, value] : j.items()) {
                            // Staged composite pack: entry carries a "stages" object.
                            if (value.contains("stages")) {
                                CompositePack pack;
                                pack.name = key;
                                if (value.contains("direction")) pack.direction = ParseDirection(value["direction"]);
                                if (value.contains("sex")) pack.sex = ParseSex(value["sex"]);
                                if (value.contains("isHungry")) pack.isHungry = value["isHungry"].get<bool>();
                                if (value.contains("furniture")) pack.furniture = ParseFurniture(value["furniture"]);

                                const auto& stages = value["stages"];
                                auto readStage = [&stages](const char* name) -> StageClips {
                                    StageClips clips;
                                    if (stages.contains(name)) {
                                        const auto& s = stages[name];
                                        if (s.contains("player")) clips.player = s["player"].get<std::string>();
                                        if (s.contains("target")) clips.target = s["target"].get<std::string>();
                                    }
                                    return clips;
                                };
                                pack.intro = readStage("intro");
                                pack.loop  = readStage("loop");
                                pack.exit  = readStage("exit");
                                pack.drained = readStage("drained");

                                // Player-only when no stage animates the target: the
                                // victim is never given a clip, so the lifecycle leaves
                                // it in place (used by the furniture bed/bedroll packs).
                                pack.playerOnly = pack.intro.target.empty() && pack.loop.target.empty() &&
                                                  pack.exit.target.empty() && pack.drained.target.empty();

                                SKSE::log::debug("Loaded composite pack: {} (furniture={}, direction={}, playerOnly={})",
                                    key, static_cast<int>(pack.furniture), static_cast<int>(pack.direction), pack.playerOnly);
                                compositePacks_.push_back(std::move(pack));
                                continue;
                            }

                            AnimationDefinition def;
                            def.eventName = key;

                            if (value.contains("direction")) def.direction = ParseDirection(value["direction"]);
                            if (value.contains("sex")) def.sex = ParseSex(value["sex"]);
                            if (value.contains("type")) def.type = ParseType(value["type"]);
                            if (value.contains("isHungry")) def.isHungry = value["isHungry"].get<bool>();
                            if (value.contains("lethal")) def.isLethal = value["lethal"].get<bool>();
                            if (value.contains("feedType")) def.feedTypeID = value["feedType"].get<int>();

                            animations_.push_back(def);
                            SKSE::log::debug("Loaded animation: {} (feedType: {})", def.eventName, def.feedTypeID);
                        }
                    } catch (const json::parse_error& e) {
                        SKSE::log::error("Failed to parse JSON {}: {}", filename, e.what());
                    }
                }
            }
        }
        SKSE::log::info("Total animations loaded: {} ({} composite packs)", animations_.size(), compositePacks_.size());
    }

    const AnimationDefinition* AnimationRegistry::GetBestMatch(const FeedContext& context) const {
        if (!context.player) return nullptr;

        std::vector<const AnimationDefinition*> candidates;

        // Player Sex determination
        Sex playerSex = Sex::Male;
        auto* base = context.player->GetBaseObject();
        auto* npc = base ? base->As<RE::TESNPC>() : nullptr;
        if (npc && npc->IsFemale()) {
            playerSex = Sex::Female;
        }

        // Check if target is hostile
        bool targetIsHostile = false;
        if (context.target && context.player) {
            targetIsHostile = context.target->IsHostileToActor(context.player);
        }

        // Determine if we should use combat animations (outside loop for efficiency)
        bool shouldUseCombat = context.isCombat || targetIsHostile || context.isLethal;

        for (const auto& anim : animations_) {
            // Combat animations are implicitly lethal (they have kills baked in)
            bool animIsLethal = anim.isLethal || (anim.type == Type::Combat);

            // 1. Lethal filter: Always apply (player combat forces isLethal upstream)
            if (context.isLethal && !animIsLethal) continue;  // User wants lethal, anim is not
            if (!context.isLethal && animIsLethal) continue;  // User wants normal, anim is lethal

            // 2. Combat type filter: Animation type must match combat context
            bool animIsCombat = (anim.type == Type::Combat);
            if (shouldUseCombat != animIsCombat) continue;

            // 3. Direction filter: Front/Back must match (Direction::Any always passes)
            if (anim.direction == Direction::Front && context.isBehind) continue;
            if (anim.direction == Direction::Back && !context.isBehind) continue;

            // 4. Hunger filter: In combat, allow all animations for variety
            //    Outside combat: Sated player can't use hungry anims, but hungry player can use both
            if (anim.isHungry && !context.isHungry && !shouldUseCombat) continue;

            // 5. Sex filter: Skip gender-specific anims that don't match player (Unisex always passes)
            if (anim.sex != Sex::Unisex && anim.sex != playerSex) continue;

            candidates.push_back(&anim);
        }

        if (candidates.empty()) return nullptr;

        // Thread-safe random generator
        thread_local std::random_device rd;
        thread_local std::mt19937 gen(rd());

        // Weighted selection: In combat, keep hungry animations "fresh" by making them rarer
        // Outside combat or when player is hungry, use uniform random
        if (shouldUseCombat && !context.isHungry) {
            // Combat + sated player: Weight non-hungry animations higher (70/30 split roughly)
            std::vector<double> weights;
            weights.reserve(candidates.size());

            for (const auto* cand : candidates) {
                double weight = 1.0;

                // Non-hungry animations: higher weight (more common)
                // Hungry animations: lower weight (kept fresh/surprising)
                if (cand->isHungry) {
                    weight = 0.3;  // ~30% relative chance
                } else {
                    weight = 1.0;  // Base weight
                }

                // Slight preference for gender-specific animations
                if (cand->sex == playerSex) {
                    weight *= 1.2;
                }

                weights.push_back(weight);
            }

            std::discrete_distribution<> dis(weights.begin(), weights.end());
            return candidates[dis(gen)];
        }

        // Default: Uniform random selection
        std::uniform_int_distribution<> dis(0, static_cast<int>(candidates.size() - 1));
        return candidates[dis(gen)];
    }

    const CompositePack* AnimationRegistry::GetBestCompositeMatch(const FeedContext& context) const {
        if (compositePacks_.empty() || !context.player) return nullptr;

        // Player sex (composite packs filter on the player, like GetBestMatch).
        Sex playerSex = Sex::Male;
        auto* base = context.player->GetBaseObject();
        auto* npc = base ? base->As<RE::TESNPC>() : nullptr;
        if (npc && npc->IsFemale()) playerSex = Sex::Female;

        std::vector<const CompositePack*> candidates;
        for (const auto& pack : compositePacks_) {
            // Furniture: the pack's furniture kind must match the victim's. This
            // keeps standing feeds (None) and bed/bedroll feeds in separate pools,
            // so a player-only furniture pack is never rolled for a standing target.
            if (pack.furniture != context.furniture) continue;
            // Direction: Front/Back match player position (upright feeds); Left/Right
            // match which side of the furniture the player is on. Any always passes.
            if (pack.direction == Direction::Front && context.isBehind) continue;
            if (pack.direction == Direction::Back && !context.isBehind) continue;
            if (pack.direction == Direction::Left && !context.playerOnLeft) continue;
            if (pack.direction == Direction::Right && context.playerOnLeft) continue;
            // Sex: skip gender-specific packs that don't match the player
            if (pack.sex != Sex::Unisex && pack.sex != playerSex) continue;
            // Hunger: a sated player can't use hungry-only packs
            if (pack.isHungry && !context.isHungry) continue;

            candidates.push_back(&pack);
        }

        if (candidates.empty()) return nullptr;

        thread_local std::random_device rd;
        thread_local std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, static_cast<int>(candidates.size() - 1));
        return candidates[dis(gen)];
    }

    bool AnimationRegistry::HasCompositeBackPack() const {
        for (const auto& pack : compositePacks_) {
            if (pack.direction == Direction::Back) return true;
        }
        return false;
    }

    const AnimationDefinition* AnimationRegistry::GetNextDebugAnimation(const FeedContext& context) {
        if (animations_.empty() || !context.player) return nullptr;

        // For debug mode, only filter by direction (front/back position)
        std::vector<const AnimationDefinition*> validAnimations;

        for (const auto& anim : animations_) {
            // Only check direction - ignore combat type, hunger, and sex
            if (anim.direction == Direction::Front && context.isBehind) continue;
            if (anim.direction == Direction::Back && !context.isBehind) continue;

            validAnimations.push_back(&anim);
        }

        if (validAnimations.empty()) {
            SKSE::log::warn("Debug Cycle: No valid animations for current context");
            return nullptr;
        }

        static size_t currentIndex = 0;
        if (currentIndex >= validAnimations.size()) {
            currentIndex = 0;
        }

        const auto* anim = validAnimations[currentIndex];
        SKSE::log::info("Debug Cycle: Playing animation {}/{} (filtered) - {}",
            currentIndex + 1, validAnimations.size(), anim->eventName);

        currentIndex++;
        return anim;
    }

    // Fallback animation selection (legacy logic for when no OAR animations are loaded)
    const char* SelectIdleAnimation(int targetState, RE::Actor* target,
                                    const RE::NiPointer<RE::TESObjectREFR>& furnitureRef, bool isBehind,
                                    bool& outIsPairedAnim, bool lethal) {
        outIsPairedAnim = true;
        auto player = RE::PlayerCharacter::GetSingleton();

        // Special handling for Werewolf and Vampire Lord
        if (player) {
            if (TargetState::IsWerewolf(player)) {
                if (targetState == kDead) {
                    // Werewolf feeding on corpse - solo animation
                    outIsPairedAnim = false;
                    SKSE::log::debug("Player is Werewolf - using corpse devour (solo)");
                    return Idles::WEREWOLF_CORPSE_FEED;
                }
                // Werewolf feeding on alive target - paired animation
                SKSE::log::debug("Player is Werewolf - using paired feed");
                return Idles::WEREWOLF_STANDING_FRONT;
            }
            if (TargetState::IsVampireLord(player)) {
                SKSE::log::debug("Player is Vampire Lord - using VL feed");
                return isBehind ? Idles::VAMPIRELORD_STANDING_BACK : Idles::VAMPIRELORD_STANDING_FRONT;
            }
        }

        // Dead targets use bedroll animation (corpse on ground)
        if (targetState == kDead) {
            outIsPairedAnim = false;
            bool isLeft = PairedAnimation::IsPlayerOnLeftSide(target);
            SKSE::log::debug("Dead target - using bedroll {} side (solo idle)", isLeft ? "left" : "right");
            return isLeft ? Idles::VAMPIRE_BEDROLL_LEFT : Idles::VAMPIRE_BEDROLL_RIGHT;
        }

        if (targetState == kSleeping && furnitureRef) {
            outIsPairedAnim = false;
            bool isLeft = PairedAnimation::IsPlayerOnLeftSide(target);
            bool isBedroll = PairedAnimation::IsBedroll(furnitureRef.get());

            if (isBedroll) {
                SKSE::log::debug("Bedroll {} side (solo idle)", isLeft ? "left" : "right");
                return isLeft ? Idles::VAMPIRE_BEDROLL_LEFT : Idles::VAMPIRE_BEDROLL_RIGHT;
            } else {
                SKSE::log::debug("Bed {} side (solo idle)", isLeft ? "left" : "right");
                return isLeft ? Idles::VAMPIRE_BED_LEFT : Idles::VAMPIRE_BED_RIGHT;
            }
        }

        const char* posStr = isBehind ? "back" : "front";

        if (targetState == kSitting) {
            SKSE::log::debug("Sitting {} feed", posStr);
            return isBehind ? Idles::VAMPIRE_SITTING_BACK : Idles::VAMPIRE_SITTING_FRONT;
        } else if (lethal) {
            // Lethal param now carries correct intent (forced by player combat or user choice)
            SKSE::log::debug("Lethal {} feed (kill move)", posStr);
            return isBehind ? Idles::BACK_SNEAK_KM_A : Idles::FRONT_KM_A;
        } else {
            // Standing or Combat (non-lethal) uses normal animations
            SKSE::log::debug("Standing {} feed", posStr);
            return isBehind ? Idles::VAMPIRE_STANDING_BACK : Idles::VAMPIRE_STANDING_FRONT;
        }
    }

}
