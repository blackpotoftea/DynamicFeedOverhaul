# Dynamic Feed Overhaul

**Advanced vampire feeding system with customizable animations, combat feeding, and smart target detection powered by SkyPrompt API**

---

## ⚠️ Early Beta Warning

**This mod is in early beta testing.** While designed to be non-save-breaking, you may encounter bugs or unexpected behavior. Please report issues with detailed logs and steps to reproduce. Regular updates and fixes are planned based on user feedback.

---

## Overview

This replace mosty dynamic feed system with SKSE plugin, that should was correclty place animation, trigger correctly vampire feed and called VampireFeed() function. I taken precaution so it should compatible with most overhaul, but if encounter problems let me know! Mod allows player to dynamicly trigger feed animations for vampire, vampire lord (werewolf are support but disable to requiring more work)

It's direct sucessor for player and comes with same vampire animations but they packaged

## Default settings
once target is blow 


---

## Requirements

### Required
- **Skyrim Special Edition or Anniversary Edition** (SE/AE)
- **SKSE64** - Script Extender
- **Address Library for SKSE** - Required for version-independent addressing
- **SkyPrompt API** - Prompt system framework
- **SKSEMenuFramework** - For icon overlay rendering
- **Open Animation Replacer (OAR)** - If using custom feeding animations


---



## Usage

### Basic Feeding
1. Become a vampire (or enable `ForceVampire` for testing)
2. Look at a valid NPC target
3. When the prompt appears, press your configured feed key
4. Animation plays and you gain vampire feeding benefits

### Tips
- **Sleeping NPCs**: Sneak up on sleeping targets for easy feeding
- **Combat Feeding**: Finish off low-health enemies with a dramatic feed
- **Height Issues**: The mod auto-adjusts heights when feeding on stairs
- **Icon Overlay**: A fang icon appears above valid targets (configurable)

---



## Troubleshooting

### Feed prompt not appearing
- Ensure you're a vampire (or set `ForceVampire = true`)
- Check target isn't excluded by keywords in `[Filtering]`
- Verify `EnableMod = true` in INI
- Look for errors in `DynamicFeedOverhaul.log`

### Animation not playing
- Confirm animation exists in OAR folders
- Check `config.json` condition matches FeedType value
- Enable `DebugLogging = true` to see which animation IDs are selected
- Use `SequentialPlay = true` to test all configured animations

### Icon not showing
- Verify `EnableIconOverlay = true`
- Check icon file exists at specified `IconPath`
- Requires SKSEMenuFramework to be installed

### Height/positioning issues
- Enable `EnableHeightAdjust = true` for stair feeding
- Adjust `TargetOffsetX/Y/Z` if using `UseTwoSingleAnimations`

---

## Compatibility

**Compatible With:**
- All vampire overhaul mods (Sacrosanct, Better Vampires, etc.)
- Custom vampire animation mods via OAR
- Vampire perk overhauls
- Gameplay overhauls (Requiem, EnaiRim, SimonRim, etc.)

**Highly Recommended:**
- **Disable vanilla vampire killmoves** - Prevents conflicts between feeding prompts and killmove triggers

**Load Order:**
Place after vampire gameplay mods to ensure compatibility.

**Known Issues (Early Beta):**
- Werewolf support is experimental, buggy, and disabled by default - enable at your own risk
- Sitting chair animations require custom behavior patches
- Some edge cases with height adjustments may need tweaking
- Icon overlay positioning may need adjustment on ultrawide monitors

---


## Credits

**Author**: [Your Name]

**Special Thanks**:
- **SkyPrompt Team** - For the excellent SkyPrompt API framework that powers the prompt system
- **SKSEMenuFramework Team** - For the menu framework enabling the icon overlay system
- **SKSE Team** - For the Script Extender that makes all of this possible
- **CommonLibSSE-NG Team** - For the fantastic modding library
- **OAR Developers** - For the animation replacement framework
- **Bethesda** - For Skyrim and the Creation Engine

---

## Permissions

- You may use this mod in mod packs with credit
- You may create patches and extensions
- You may NOT re-upload this mod to other sites


---

## Changelog

### Version 0.9.0 Beta (Initial Release)
- initial beta release

---

## Support

**This is an early beta - bugs are expected!**

**Bug Reports**: Please post in the Bugs section with:
- Detailed description of the issue
- Steps to reproduce
- Your `DynamicFeedOverhaul.log` from `Documents/My Games/Skyrim Special Edition/SKSE/`
- Your INI configuration (if modified)
- List of relevant mods (vampire mods, animation mods, etc.)

**Questions**: Post in the Posts section

**Enable Debug Logging**: Set `DebugLogging = true` in the INI for more detailed logs when reporting issues

---

## Future Plans
- add custom inmation

---

## Notes

- **Vanilla Animation Compatible**: Works perfectly with vanilla vampire feeding animations - no custom animations required!
- **Disable Killmoves**: For best experience, disable vampire killmoves to prevent conflicts
- **Early Beta**: Please be patient with bugs and provide detailed feedback
- **Not Save-Breaking**: Should be safe to uninstall, but always backup your saves

---

**Enjoy your dynamic vampire feeding experience!**

*This mod is powered by the SkyPrompt API framework for seamless prompt integration. Big thanks to the SkyPrompt, SKSEMenuFramework, and SKSE teams for making this mod possible!*
