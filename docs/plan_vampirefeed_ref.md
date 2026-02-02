# Plan: Reimplement Vampire Feed with Full Control

## Problem
When `player->InitiateVampireFeedPackage()` is called and something goes wrong (e.g., NPC dies mid-animation), both player and NPC can get stuck. The vanilla function fires-and-forgets with no way to recover.

## Solution: Reimplement Vampire Feed Ourselves

Instead of using `InitiateVampireFeedPackage()`, we implement the paired animation ourselves using CommonLibSSE functions. This gives us full control to:
- Detect when animation fails to start
- Force-stop when things go wrong
- Clean up state properly

## Key CommonLibSSE Functions Found

From [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG):

### Actor / PlayerCharacter
```cpp
// Put player in AI-driven state (loses control during animation)
void PlayerCharacter::SetAIDriven(bool a_enable);

// Vampire feed (what we're replacing)
void Actor::InitiateVampireFeedPackage(Actor* a_target, TESObjectREFR* a_furniture);
```

### AIProcess (for playing idles)
```cpp
// Play an idle animation with optional target
bool AIProcess::PlayIdle(Actor* a_actor, TESIdleForm* a_idle, TESObjectREFR* a_target);

// Setup special idle with more control
bool AIProcess::SetupSpecialIdle(Actor* a_actor, DEFAULT_OBJECT a_action,
                                  TESIdleForm* a_idle, bool a_arg5, bool a_arg6,
                                  TESObjectREFR* a_target);

// Stop current idle
void AIProcess::StopCurrentIdle(Actor* a_actor, bool a_forceIdleStop);
```

### Existing in Codebase
```cpp
// Already have this - detect if in paired animation
bool PairedAnimation::IsInPairedAnimation(const RE::Actor* actor);
```

## Implementation Approach

### Step 0: Create Pluggable Feed Interface

First, create a separate abstraction layer that allows swapping between vanilla and custom feed implementations:

```cpp
// IVampireFeed.h - Interface for vampire feed implementations
class IVampireFeed {
public:
    virtual ~IVampireFeed() = default;

    // Start the feed on target (returns true if started successfully)
    virtual bool StartFeed(RE::Actor* target, RE::TESObjectREFR* furniture = nullptr) = 0;

    // Force stop the current feed (cleanup/recovery)
    virtual void StopFeed() = 0;

    // Check if currently feeding
    virtual bool IsFeeding() const = 0;

    // Update tick (for monitoring state)
    virtual void Update() = 0;
};

// VanillaVampireFeed.h - Wrapper around vanilla implementation
class VanillaVampireFeed : public IVampireFeed {
public:
    bool StartFeed(RE::Actor* target, RE::TESObjectREFR* furniture) override {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !target) return false;

        player->InitiateVampireFeedPackage(target, furniture);
        feeding_ = true;
        return true;
    }

    void StopFeed() override {
        // Vanilla has no clean stop - this is why we need custom impl
        feeding_ = false;
    }

    bool IsFeeding() const override { return feeding_; }
    void Update() override { /* No monitoring in vanilla */ }

private:
    bool feeding_ = false;
};

// CustomVampireFeed.h - Our reimplementation with full control
class CustomVampireFeed : public IVampireFeed {
    // Full implementation below in Step 1-4
};

// FeedManager - Singleton that holds the active implementation
class FeedManager {
public:
    static FeedManager& GetSingleton() {
        static FeedManager instance;
        return instance;
    }

    void SetImplementation(std::unique_ptr<IVampireFeed> impl) {
        impl_ = std::move(impl);
    }

    IVampireFeed* Get() { return impl_.get(); }

    // Convenience methods
    bool StartFeed(RE::Actor* target, RE::TESObjectREFR* furniture = nullptr) {
        return impl_ ? impl_->StartFeed(target, furniture) : false;
    }

    void StopFeed() { if (impl_) impl_->StopFeed(); }
    void Update() { if (impl_) impl_->Update(); }

private:
    std::unique_ptr<IVampireFeed> impl_;
};
```

**Usage - Easy swap between implementations:**
```cpp
// In plugin init - use vanilla for now
FeedManager::GetSingleton().SetImplementation(std::make_unique<VanillaVampireFeed>());

// Later, switch to custom when ready
FeedManager::GetSingleton().SetImplementation(std::make_unique<CustomVampireFeed>());

// In VampireFeedSink - just call the manager
FeedManager::GetSingleton().StartFeed(target, furniture);
```

This allows:
- Testing vanilla vs custom side-by-side
- Easy rollback if custom has issues
- Runtime switching via console command or MCM
- Clean separation of concerns

---

### Step 1: Create Custom Feed Function
Replace `InitiateVampireFeedPackage` with our own implementation:

```cpp
bool StartVampireFeed(RE::Actor* target, RE::TESObjectREFR* furniture) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !target) return false;

    // Store state for recovery
    feedTarget_ = target;
    feedStartTime_ = GetCurrentTime();

    // Put player in AI-driven state
    player->SetAIDriven(true);

    // Get vampire feed idle form (need to look up FormID)
    auto* feedIdle = RE::TESForm::LookupByID<RE::TESIdleForm>(VAMPIRE_FEED_IDLE_FORMID);

    // Play paired animation on both actors
    auto* playerProcess = player->GetActorRuntimeData().currentProcess;
    if (playerProcess && feedIdle) {
        playerProcess->PlayIdle(player, feedIdle, target);
    }

    return true;
}
```

### Step 2: Monitor Feed State
Check for problems during feed:

```cpp
void CheckFeedState() {
    if (!feedTarget_) return;

    auto* player = RE::PlayerCharacter::GetSingleton();

    // Check if animation failed to start
    if (!IsInPairedAnimation(player)) {
        // Animation didn't start - cleanup
        ForceStopFeed();
        return;
    }

    // Check if target died
    if (feedTarget_->IsDead()) {
        ForceStopFeed();
        return;
    }

    // Check timeout
    if (GetCurrentTime() - feedStartTime_ > MAX_FEED_DURATION) {
        ForceStopFeed();
        return;
    }
}
```

### Step 3: Force Stop Function
Clean exit from feed animation:

```cpp
void ForceStopFeed() {
    auto* player = RE::PlayerCharacter::GetSingleton();

    // Restore player control
    player->SetAIDriven(false);

    // Stop idle animations
    if (auto* process = player->GetActorRuntimeData().currentProcess) {
        process->StopCurrentIdle(player, true);
    }
    if (feedTarget_) {
        if (auto* process = feedTarget_->GetActorRuntimeData().currentProcess) {
            process->StopCurrentIdle(feedTarget_, true);
        }
    }

    // Clear graph variables
    ClearFeedGraphVars(player);
    ClearFeedGraphVars(feedTarget_);

    // Reset state
    feedTarget_ = nullptr;
    feedStartTime_ = 0;
}
```

### Step 4: Listen for Animation End Event
Register for `VampireFeedEnd` to know when animation completes normally:

```cpp
class FeedAnimationSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    RE::BSEventNotifyControl ProcessEvent(
        const RE::BSAnimationGraphEvent* event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
    {
        if (event && event->tag == "VampireFeedEnd") {
            OnFeedComplete();
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
```

## Files to Modify/Create

3. **src/CustomVampireFeed.h** (NEW)
   - Our full reimplementation with recovery logic
   - State tracking: `feedTarget_`, `feedStartTime_`
   - `FeedAnimationSink` for animation events

4. **src/FeedManager.h** (NEW)
   - Singleton holding active `IVampireFeed` implementation
   - Convenience methods for feed operations
   - Runtime swap capability

5. **src/VampireFeedSink.h**
   - Replace direct `InitiateVampireFeedPackage` call with `FeedManager::GetSingleton().StartFeed()`

6. **src/hook.cpp**
   - Call `FeedManager::GetSingleton().Update()` periodically (on crosshair events or timer)

7. **src/Settings.h** (optional)
   - Add `MaxFeedDuration` setting
   - Add `UseCustomFeedImplementation` toggle

## Open Questions

1. **Vampire Feed Idle FormID**: Need to find the TESIdleForm for vampire feeding. Options:
   - Look up in Creation Kit
   - Use xEdit to find in Skyrim.esm
   - Do you know the FormID or EditorID?

2. **Front vs Back Animation**: How does vanilla decide front/back? We may need separate idles or use the furniture reference.

3. **Furniture Handling**: For sleeping/sitting targets, how should we handle the furniture reference?
