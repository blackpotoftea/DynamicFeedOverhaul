#pragma once
#include "SKSEMCP/SKSEMenuFramework.hpp"

namespace UI {
    void Register();

    namespace Debug {
        void __stdcall Render();
    }

    namespace Settings {
        void __stdcall Render();
    }
}
