# Dynamic Feed Overhaul

**A complete vampire feeding overhaul with contextual prompts, custom animations, and full vampire mod compatibility**

---

## What This Mod Does

Dynamic Feed Overhaul replaces Skyrim's clunky vampire feeding system with a modern, intuitive experience. Instead of awkward dialogue menus or unreliable activation prompts, you get a clean on-screen prompt that appears when looking at valid targets.

**Feed on anyone, anywhere** - standing NPCs, sleeping victims, enemies in combat, or even fresh corpses. The mod handles positioning, animations, and integrates seamlessly with popular vampire overhauls.

---

## Key Features

### Contextual Feed Prompt
- Clean UI prompt appears when targeting valid NPCs
- Supports keyboard and gamepad
- Configurable delay prevents prompt spam when scanning crowds
- Optional "facing target" requirement for immersion

### Multiple Feed Contexts
- **Standing NPCs** - Feed on unaware targets with front/back approach detection
- **Sleeping Victims** - Classic bedside feeding with left/right positioning
- **Combat Feeding** - Finish off weakened enemies mid-battle
- **Corpse Feeding** - Drain the recently deceased (time-limited)

### Lethal Feed Option
- Hold the feed button to kill non-essential targets
- Quick press for normal feeding
- Essential NPCs automatically excluded from lethal option

### Combat Feeding
- Feed on enemies during combat when they're low on health
- Optional stagger detection - feed on staggered enemies regardless of health
- Witness detection stops feeding if other NPCs spot you

### Smart Target Filtering
- Exclude automatons, undead, ghosts, and other invalid targets by keyword
- Exclude specific NPCs by form ID (e.g., Serana)
- Level-based filtering option
- OStim NG scene detection - won't interrupt adult scenes

### Animation Support
- Works with vanilla feeding animations out of the box
- Full Open Animation Replacer (OAR) support for custom animations
- Automatic height adjustment for feeding on stairs
- Player/target rotation for proper animation alignment

### Visual Feedback
- Optional vampire fang icon above valid targets
- Customizable icon position, size, and duration

### Vampire Mod Integration
- Auto-detects Sacrosanct, Better Vampires, and other overhauls
- Calls proper VampireFeed() functions for full compatibility
- Emits mod events for custom integrations

---

## Requirements

### Required
- **Skyrim SE/AE** (1.5.97 or 1.6.x)
- **SKSE64**
- **Address Library for SKSE**
- **SkyPrompt** - Powers the prompt system

### Optional
- **Open Animation Replacer (OAR)** - For custom feeding animations
- **SKSEMenuFramework** - For icon overlay feature

---

## Installation

1. Install all requirements
2. Install this mod with your mod manager
3. (Optional) Edit `Data/SKSE/Plugins/DynamicFeedOverhaul.ini` to customize settings

---

## Configuration Highlights

The INI file offers extensive customization:

```ini
[General]
EnableMod = true              ; Master toggle
CheckHungerStage = true       ; Only feed when hungry
MinHungerStage = 2            ; Minimum hunger (1=sated, 4=starving)

[NonCombat]
AllowStanding = true          ; Feed on standing NPCs
AllowSleeping = true          ; Feed on sleeping NPCs
EnableLethalFeed = true       ; Hold-to-kill feature
LethalHoldDuration = 5.0      ; Seconds to hold for lethal

[Combat]
Enabled = true                ; Combat feeding
RequireLowHealth = false      ; Require low health target
LowHealthThreshold = 0.25     ; 25% health threshold
AllowStaggered = true         ; Feed on staggered enemies

[Filtering]
ExcludeKeywords = ActorTypeDwarven, ActorTypeDraugr, ActorTypeGhost
ExcludeActorIDs = Dawnguard.esm|0x002B6C  ; Exclude Serana
```

See the INI file for all options with detailed comments.

---

## Compatibility

**Fully Compatible:**
- Sacrosanct - Vampires of Skyrim
- Better Vampires
- Vampire gameplay overhauls (Requiem, EnaiRim, SimonRim)
- Custom vampire animation packs via OAR
- OStim NG (auto-detects scenes)

**Recommended:**
- Disable vanilla vampire killmoves to prevent prompt conflicts

**Load Order:**
Place after vampire gameplay mods.

---

## Troubleshooting

**Prompt not appearing?**
- Verify you're a vampire (or set `ForceVampire = true` for testing)
- Check `EnableMod = true` in INI
- Target may be excluded by keywords or actor ID

**Animation issues?**
- Enable `DebugLogging = true` in INI
- Check `DynamicFeedOverhaul.log` in SKSE logs folder
- For OAR animations, verify condition files match FeedType values

**Height/position problems?**
- Enable `EnableHeightAdjust = true`
- Adjust offset values if using custom animations

---

## Known Limitations

- Werewolf support is experimental and disabled by default
- Chair feeding requires custom behavior patches (disabled by default)
- Vampire Lord uses standard feeding (no unique drain mechanics)

---

## Credits

- **SkyPrompt Team** - Prompt framework
- **SKSE Team** - Script Extender
- **CommonLibSSE-NG Team** - Modding library
- **OAR Developers** - Animation framework

---

## Permissions

- Mod packs: Yes, with credit
- Patches/extensions: Yes
- Re-uploads: No

---

## Support

**Bug Reports** - Include:
- Description and reproduction steps
- `DynamicFeedOverhaul.log` from SKSE logs
- Your INI settings
- Relevant mod list

Enable `DebugLogging = true` for detailed logs.

---

*Powered by SkyPrompt API*
