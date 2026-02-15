#pragma once
#include "RE/Skyrim.h"

namespace MenuCheck {
    // Check if a specific menu name is in the blocklist
    bool IsMenuBlocked(const RE::BSFixedString& menuName);

    // Check if any blocked menu is currently open (also checks GameIsPaused)
    bool IsAnyBlockedMenuOpen();
}
