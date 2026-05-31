# Dynamic Feed Overhaul

An SKSE plugin for Skyrim Special Edition that overhauls the vampire feeding system with custom paired animations and contextual prompts via SkyPrompt integration.

## Features

- Custom paired feeding animations with support for multiple animation packs
- SkyPrompt integration for contextual feeding prompts
- Witness detection system
- HUD icon overlay for feed targets
- Compatibility with vampire overhaul mods:
  - Sacrosanct
  - Sacrilege
- Optional integrations:
  - OStim NG (scene exclusion)
  - Poise mod (stagger behavior)

## Requirements

- [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community edition or higher)
- [CMake](https://cmake.org/) 3.21+
- [vcpkg](https://github.com/microsoft/vcpkg)

## Dependencies

Managed via vcpkg:
- **CommonLibSSE-NG** - SKSE plugin framework supporting SE, AE, GOG, and VR
- **SkyPrompt API** - Contextual prompt system
- **SKSE-MCP** - Menu framework for HUD elements
- **nlohmann-json** - JSON parsing for animation configs
- **SimpleINI** - INI file handling for settings

## Building

### 1. Setup vcpkg

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat
```

Set the `VCPKG_ROOT` environment variable to your vcpkg folder path.

### 2. Build the plugin

Open the project folder in Visual Studio 2022, VS Code (with C++ and CMake Tools extensions), or CLion. CMake will automatically configure and fetch dependencies.

Or build from command line:
```bash
cmake --preset release-msvc
cmake --build build/release-msvc --config Release
```

The compiled `.dll` will be in the `build/` folder.

### 3. Deploy (optional)

Set one of these environment variables to auto-deploy on build:
- `SKYRIM_FOLDER` - Path to Skyrim SE installation (deploys to `Data/`)
- `SKYRIM_MODS_FOLDER` - Path to mod manager mods folder (MO2/Vortex)

## Debugging

1. Use a Steamless-stripped Skyrim executable
2. MO2 users: add `-forcesteamloader` to SKSE arguments
3. VS Code: use the attach debugger configuration in `launch.json`
