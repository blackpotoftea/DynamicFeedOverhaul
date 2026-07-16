<#
.SYNOPSIS
    Assemble a distributable ZIP of DynamicFeedOverhaul from the repo + build output.

.DESCRIPTION
    Mirrors the in-game Data layout into a clean staging folder (SKSE\, Interface\,
    meshes\ at the archive root) and compresses it to dist\DynamicFeedOverhaul-v<ver>.zip.
    Pulls only the runtime files from the repo; dev junk (.psd, .lib, meta.ini, the
    meshes\0SA test tree, .pdb) is excluded by omission. Does NOT build - it consumes
    the existing build output and errors clearly if the DLL is missing.
#>
[CmdletBinding()]
param(
    # Add the (large) debug symbols so crash logs resolve to function names.
    [switch]$IncludePdb,
    # Defaults to the version-string from vcpkg.json; used in the zip filename.
    [string]$Version,
    # Where the finished zip lands.
    [string]$OutDir = (Join-Path $PSScriptRoot 'dist')
)

$ErrorActionPreference = 'Stop'

$src = Join-Path $PSScriptRoot 'build\relwithdebinfo-msvc\RelWithDebInfo'
$dll = Join-Path $src 'DynamicFeedOverhaul.dll'
$pdb = Join-Path $src 'DynamicFeedOverhaul.pdb'

if (-not (Test-Path -LiteralPath $dll)) {
    throw "Build output not found: $dll`nRun build.ps1 first to compile the plugin."
}
if ($IncludePdb -and -not (Test-Path -LiteralPath $pdb)) {
    throw "PDB requested but not found: $pdb`nRun build.ps1 first, or omit -IncludePdb."
}

# Resolve the package version from vcpkg.json unless the caller overrode it.
if (-not $Version) {
    $vcpkg = Join-Path $PSScriptRoot 'vcpkg.json'
    if (Test-Path -LiteralPath $vcpkg) {
        $Version = (Get-Content -LiteralPath $vcpkg -Raw | ConvertFrom-Json).'version-string'
    }
    if (-not $Version) { $Version = '0.0.0' }
}

# Clean staging dir - building the layout here means exclusions happen by omission.
$staging = Join-Path $OutDir '_staging\DynamicFeedOverhaul'
if (Test-Path -LiteralPath $staging) {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
New-Item -ItemType Directory -Path $staging -Force | Out-Null

# Copy a single file to a path inside staging, creating parent dirs as needed.
function Copy-Into([string]$Source, [string]$RelativeDest) {
    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required source file not found: $Source"
    }
    $dest = Join-Path $staging $RelativeDest
    $parent = Split-Path -Parent $dest
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $dest -Force
}

# --- SKSE\Plugins -------------------------------------------------------------
Copy-Into $dll 'SKSE\Plugins\DynamicFeedOverhaul.dll'
if ($IncludePdb) {
    Copy-Into $pdb 'SKSE\Plugins\DynamicFeedOverhaul.pdb'
}
Copy-Into (Join-Path $PSScriptRoot 'DynamicFeedOverhaul.ini') 'SKSE\Plugins\DynamicFeedOverhaul.ini'
Copy-Into (Join-Path $PSScriptRoot 'main_DFO.json')           'SKSE\Plugins\main_DFO.json'
Copy-Into (Join-Path $PSScriptRoot 'main_c_DFO.json')         'SKSE\Plugins\main_c_DFO.json'
Copy-Into (Join-Path $PSScriptRoot 'BehaviorDataInjector\DynamicFeedOverhaul_BDI.json') `
          'SKSE\Plugins\BehaviorDataInjector\DynamicFeedOverhaul_BDI.json'

# --- Interface\ImGuiIcons\Icons (png only, skip the .psd art sources) ---------
$iconsSrc = Join-Path $PSScriptRoot 'Interface\ImGuiIcons\Icons'
$pngFiles = Get-ChildItem -LiteralPath $iconsSrc -Filter '*.png' -File
if ($pngFiles.Count -eq 0) { throw "No PNG icons found in $iconsSrc" }
foreach ($png in $pngFiles) {
    Copy-Into $png.FullName "Interface\ImGuiIcons\Icons\$($png.Name)"
}

# --- meshes (anim trees + FNIS behavior; 0SA test tree is never referenced) ---
$meshRoot = 'meshes\actors\character'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "$meshRoot\animations\DynamicFeedOverhaul") `
          -Destination (Join-Path $staging "$meshRoot\animations\DynamicFeedOverhaul") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "$meshRoot\animations\OpenAnimationReplacer") `
          -Destination (Join-Path $staging "$meshRoot\animations\OpenAnimationReplacer") -Recurse -Force
Copy-Into (Join-Path $PSScriptRoot "$meshRoot\behaviors\FNIS_DynamicFeedOverhaul_Behavior.hkx") `
          "$meshRoot\behaviors\FNIS_DynamicFeedOverhaul_Behavior.hkx"

# --- Scripts (Papyrus detection header) ---------------------------------------
# The compiled .pex is what the game loads for DynamicFeedOverhaul.IsInstalled();
# ship the .psc source alongside it for integrators. Warn (don't silently omit)
# if the .pex was never compiled - run compile_papyrus.ps1 first.
$pex = Join-Path $PSScriptRoot 'Scripts\DynamicFeedOverhaul.pex'
if (Test-Path -LiteralPath $pex) {
    Copy-Into $pex 'Scripts\DynamicFeedOverhaul.pex'
    Copy-Into (Join-Path $PSScriptRoot 'Scripts\Source\DynamicFeedOverhaul.psc') `
              'Scripts\Source\DynamicFeedOverhaul.psc'
} else {
    Write-Warning "Papyrus .pex missing (run compile_papyrus.ps1): $pex - the zip will NOT be Papyrus-detectable"
}

# Defensive prune: drop any art sources / linker artifacts that slipped into a tree.
# Filter with Where-Object, not -Include: -Include is silently ignored alongside
# -LiteralPath, which would match (and delete) every file.
Get-ChildItem -LiteralPath $staging -Recurse -File |
    Where-Object { $_.Extension -in '.psd', '.lib', '.exp' -or $_.Name -eq 'meta.ini' } |
    Remove-Item -Force

# --- Compress -----------------------------------------------------------------
if (-not (Test-Path -LiteralPath $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}
$zip = Join-Path $OutDir "DynamicFeedOverhaul-v$Version.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }

# Use ZipFile over Compress-Archive: the latter silently drops files in PS 5.1 when
# the source wildcard expands to subdirectories. CreateFromDirectory recurses fully
# and places the staging *contents* at the archive root (no base-dir prefix).
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $staging, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)

Remove-Item -LiteralPath (Join-Path $OutDir '_staging') -Recurse -Force

$sizeMB = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
Write-Host "Created $zip ($sizeMB MB)$(if ($IncludePdb) { ' [with PDB]' })"
