#include "hooks/hook.h"
#include "feed/PairedAnimPromptSink.h"
#include "Settings.h"
#include "utils/MenuCheck.h"

namespace {

    //
    // Add period check that into OnUpdate main thread to verify taget status (dead, in other animation, scen etc)
    //
    struct PlayerUpdateHook {
        static void thunk(RE::PlayerCharacter* a_this, float a_delta) {
            func(a_this, a_delta);

            // Rate limit the periodic check to avoid excessive overhead
            static float timer = 0.0f;
            timer += a_delta;
            if (timer > Settings::GetSingleton()->General.PeriodicCheckInterval) { 
                timer = 0.0f;
                PairedAnimPromptSink::GetSingleton()->OnPeriodicValidation();
            }
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    //
    // Block UI elements from showing the prompt/overlay
    //
    class MenuOpenCloseHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuOpenCloseHandler* GetSingleton() {
            static MenuOpenCloseHandler singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            if (a_event && a_event->opening) {
                if (MenuCheck::IsMenuBlocked(a_event->menuName)) {
                    PairedAnimPromptSink::GetSingleton()->OnMenuStateChange(true);
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        MenuOpenCloseHandler() = default;
    };
}

namespace Hooks {
    void Install() {
        auto crosshairSource = SKSE::GetCrosshairRefEventSource();
        if (crosshairSource) {
            crosshairSource->AddEventSink(CrosshairRefHandler::GetSingleton());
            SKSE::log::info("Crosshair ref event handler registered");
        } else {
            SKSE::log::error("Failed to get crosshair event source!");
        }

        // Relocation funciunt
        REL::Relocation<uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
        PlayerUpdateHook::func = vtbl.write_vfunc(0xAD, PlayerUpdateHook::thunk);
        SKSE::log::info("Player update hook registered for periodic validation");

        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink(MenuOpenCloseHandler::GetSingleton());
            SKSE::log::info("MenuOpenCloseHandler registered");
        }
    }
    
    //
    // Processing cross-hair events to figure to show prompt/overlay or not
    //
    RE::BSEventNotifyControl CrosshairRefHandler::ProcessEvent(
        const SKSE::CrosshairRefEvent* event,
        [[maybe_unused]] RE::BSTEventSource<SKSE::CrosshairRefEvent>* source)
    {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto ref = event->crosshairRef.get();
        RE::Actor* actor = nullptr;

        if (ref && ref->Is(RE::FormType::ActorCharacter)) {
            actor = ref->As<RE::Actor>();
        }

        PairedAnimPromptSink::GetSingleton()->OnCrosshairUpdate(actor);

        return RE::BSEventNotifyControl::kContinue;
    }
}
