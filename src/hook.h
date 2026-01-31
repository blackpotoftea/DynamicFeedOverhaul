#pragma once

namespace Hooks {
    void Install();

    class CrosshairRefHandler : public RE::BSTEventSink<SKSE::CrosshairRefEvent> {
    public:
        static CrosshairRefHandler* GetSingleton() {
            static CrosshairRefHandler singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::CrosshairRefEvent* event,
            RE::BSTEventSource<SKSE::CrosshairRefEvent>* source) override;

    private:
        CrosshairRefHandler() = default;
        CrosshairRefHandler(const CrosshairRefHandler&) = delete;
        CrosshairRefHandler(CrosshairRefHandler&&) = delete;
        CrosshairRefHandler& operator=(const CrosshairRefHandler&) = delete;
        CrosshairRefHandler& operator=(CrosshairRefHandler&&) = delete;
    };
}
