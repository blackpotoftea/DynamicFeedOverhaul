#include "utils/MenuCheck.h"
#include "PCH.h"
#include <array>

namespace MenuCheck {
    // List of menus that should block the feed prompt
    // Defined locally here to keep headers clean
    static const std::array<RE::BSFixedString, 25> blockedMenus = {
        RE::DialogueMenu::MENU_NAME,    RE::JournalMenu::MENU_NAME,    RE::MapMenu::MENU_NAME,
        RE::StatsMenu::MENU_NAME,       RE::ContainerMenu::MENU_NAME,  RE::InventoryMenu::MENU_NAME,
        RE::TweenMenu::MENU_NAME,       RE::TrainingMenu::MENU_NAME,   RE::TutorialMenu::MENU_NAME,
        RE::LockpickingMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME,  RE::LevelUpMenu::MENU_NAME,
        RE::Console::MENU_NAME,         RE::BookMenu::MENU_NAME,       RE::CreditsMenu::MENU_NAME,
        RE::LoadingMenu::MENU_NAME,     RE::MessageBoxMenu::MENU_NAME, RE::MainMenu::MENU_NAME,
        RE::RaceSexMenu::MENU_NAME,     RE::FavoritesMenu::MENU_NAME,
        RE::CraftingMenu::MENU_NAME,    RE::BarterMenu::MENU_NAME,     RE::GiftMenu::MENU_NAME,
        "LootMenu",                     "CustomMenu"
    };

    bool IsMenuBlocked(const RE::BSFixedString& menuName) {
        for (const auto& blocked : blockedMenus) {
            if (menuName == blocked) return true;
        }
        return false;
    }

    bool IsAnyBlockedMenuOpen() {
        const auto ui = RE::UI::GetSingleton();
        if (!ui) return false;

        // Fast check: is game paused? (Covers most full screen menus)
        if (ui->GameIsPaused()) return true;

        // Specific check for unpaused menus (Dialogue, etc.)
        for (const auto& blocked : blockedMenus) {
            if (ui->IsMenuOpen(blocked)) return true;
        }
        return false;
    }
}
