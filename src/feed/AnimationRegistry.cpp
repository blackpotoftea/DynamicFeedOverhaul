#include "PCH.h"
#include "feed/AnimationRegistry.h"
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
    }

    // Helper to parse enums from string
    Direction ParseDirection(const std::string& str) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "back") return Direction::Back;
        if (s == "any") return Direction::Any;
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

    void AnimationRegistry::LoadAnimations(const std::string& directoryPath) {
        if (!fs::exists(directoryPath)) {
            SKSE::log::warn("Animation directory not found: {}", directoryPath);
            return;
        }

        SKSE::log::info("Loading animations from: {}", directoryPath);

        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                // Check for suffix _DPA.json
                if (filename.length() >= 9 && 
                    filename.substr(filename.length() - 9) == "_DPA.json") {
                    
                    try {
                        std::ifstream i(entry.path());
                        json j;
                        i >> j;

                        for (auto& [key, value] : j.items()) {
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
        SKSE::log::info("Total animations loaded: {}", animations_.size());
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

            // 1. Lethal filter: In non-combat situations, match lethal preference
            if (!context.isCombat) {
                if (context.isLethal && !animIsLethal) continue;  // User wants lethal, anim is not
                if (!context.isLethal && animIsLethal) continue;  // User wants normal, anim is lethal
            }

            // 2. Combat type filter: Animation type must match combat context
            bool animIsCombat = (anim.type == Type::Combat);
            if (shouldUseCombat != animIsCombat) continue;

            // 3. Direction filter: Front/Back must match (Direction::Any always passes)
            if (anim.direction == Direction::Front && context.isBehind) continue;
            if (anim.direction == Direction::Back && !context.isBehind) continue;

            // 4. Hunger filter: Sated player can't use hungry anims, but hungry player can use both
            if (anim.isHungry && !context.isHungry) continue;

            // 5. Sex filter: Skip gender-specific anims that don't match player (Unisex always passes)
            if (anim.sex != Sex::Unisex && anim.sex != playerSex) continue;

            candidates.push_back(&anim);
        }

        if (candidates.empty()) return nullptr;

        // Weighted Selection / Random
        // Priority: Specific Gender > Unisex
        // Priority: Hungry > Sated (if hungry)
        
        // Filter candidates for best match
        std::vector<const AnimationDefinition*> bestCandidates;
        int bestScore = -1;

        for (const auto* cand : candidates) {
            int score = 0;

            // Prioritize lethal animations if user wants lethal feed
            if (context.isLethal && cand->isLethal) score += 10; // Highest priority for lethal match

            if (cand->sex == playerSex) score += 2; // Specific gender match
            if (cand->sex == Sex::Unisex) score += 1;

            if (context.isHungry && cand->isHungry) score += 2; // Prefer hungry anims if hungry
            if (!cand->isHungry) score += 1;

            if (score > bestScore) {
                bestScore = score;
                bestCandidates.clear();
                bestCandidates.push_back(cand);
            } else if (score == bestScore) {
                bestCandidates.push_back(cand);
            }
        }

        if (bestCandidates.empty()) return nullptr;

        // Pick random from best (thread-safe)
        thread_local std::random_device rd;
        thread_local std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, static_cast<int>(bestCandidates.size() - 1));

        return bestCandidates[dis(gen)];
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

}
