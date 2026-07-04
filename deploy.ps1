[CmdletBinding()]
param(
    [switch]$NV
)

$ErrorActionPreference = 'Stop'

$source = Join-Path $PSScriptRoot 'build\relwithdebinfo-msvc\RelWithDebInfo'

if ($NV) {
    $modRoot = 'D:\wabjack\skyrim\nv\MODS\mods\DynamicFeedOverhaul'
} else {
    $modRoot = 'D:\mods\ModOrganizer\Portable_Skyrim_Special_Edition\mods\DynamicFeedOverhaul'
}

$destination = Join-Path $modRoot 'SKSE\Plugins'

$dll = Join-Path $source 'DynamicFeedOverhaul.dll'
$pdb = Join-Path $source 'DynamicFeedOverhaul.pdb'

foreach ($file in @($dll, $pdb)) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw "Source file not found: $file"
    }
}

if (-not (Test-Path -LiteralPath $destination)) {
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
}

Copy-Item -LiteralPath $dll -Destination $destination -Force
Copy-Item -LiteralPath $pdb -Destination $destination -Force

Write-Host "Copied DynamicFeedOverhaul.dll and .pdb to $destination"

$iconsRelative = 'Interface\ImGuiIcons\Icons'
$iconsSource = Join-Path $PSScriptRoot $iconsRelative
$iconsDestination = Join-Path $modRoot $iconsRelative

if (Test-Path -LiteralPath $iconsSource) {
    $pngFiles = Get-ChildItem -LiteralPath $iconsSource -Filter '*.png' -File

    if ($pngFiles.Count -gt 0) {
        if (-not (Test-Path -LiteralPath $iconsDestination)) {
            New-Item -ItemType Directory -Path $iconsDestination -Force | Out-Null
        }

        foreach ($png in $pngFiles) {
            Copy-Item -LiteralPath $png.FullName -Destination $iconsDestination -Force
        }

        Write-Host "Copied $($pngFiles.Count) PNG icon(s) to $iconsDestination"
    } else {
        Write-Host "No PNG icons found in $iconsSource"
    }
} else {
    Write-Host "Icons source directory not found: $iconsSource"
}

$animRelative = 'meshes\actors\character\animations\DynamicFeedOverhaul'
$animSource = Join-Path $PSScriptRoot $animRelative
$animDest = Join-Path $modRoot $animRelative

if (Test-Path -LiteralPath $animSource) {
    # Mirror: drop a stale copy first so removed/renamed clips don't linger, and
    # copy to the full destination path (which now does not exist) to avoid the
    # PowerShell Copy-Item folder-nesting quirk on repeat deploys.
    if (Test-Path -LiteralPath $animDest) {
        Remove-Item -LiteralPath $animDest -Recurse -Force
    }
    $animDestParent = Split-Path -Parent $animDest
    if (-not (Test-Path -LiteralPath $animDestParent)) {
        New-Item -ItemType Directory -Path $animDestParent -Force | Out-Null
    }

    Copy-Item -LiteralPath $animSource -Destination $animDest -Recurse -Force

    $hkxCount = (Get-ChildItem -LiteralPath $animSource -Filter '*.hkx' -File -Recurse).Count
    Write-Host "Copied $hkxCount animation file(s) to $animDest"
} else {
    Write-Host "Animation source directory not found: $animSource"
}

$behaviorRelative = 'meshes\actors\character\behaviors\FNIS_DynamicFeedOverhaul_Behavior.hkx'
$behaviorSource = Join-Path $PSScriptRoot $behaviorRelative
$behaviorDest = Join-Path $modRoot $behaviorRelative

if (Test-Path -LiteralPath $behaviorSource) {
    $behaviorDestParent = Split-Path -Parent $behaviorDest
    if (-not (Test-Path -LiteralPath $behaviorDestParent)) {
        New-Item -ItemType Directory -Path $behaviorDestParent -Force | Out-Null
    }

    Copy-Item -LiteralPath $behaviorSource -Destination $behaviorDest -Force

    Write-Host "Copied FNIS behavior to $behaviorDest"
} else {
    Write-Host "FNIS behavior not found (run GenerateFNIS_for_Modders first): $behaviorSource"
}

$bdiRelative = 'BehaviorDataInjector'
$bdiSource = Join-Path $PSScriptRoot $bdiRelative
$bdiDest = Join-Path $destination $bdiRelative

if (Test-Path -LiteralPath $bdiSource) {
    # Mirror so removed/renamed BDI configs don't linger in the deployed mod.
    if (Test-Path -LiteralPath $bdiDest) {
        Remove-Item -LiteralPath $bdiDest -Recurse -Force
    }
    if (-not (Test-Path -LiteralPath $destination)) {
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
    }

    Copy-Item -LiteralPath $bdiSource -Destination $bdiDest -Recurse -Force

    $jsonCount = (Get-ChildItem -LiteralPath $bdiSource -Filter '*.json' -File -Recurse).Count
    Write-Host "Copied $jsonCount BehaviorDataInjector config(s) to $bdiDest"
} else {
    Write-Host "BehaviorDataInjector source directory not found: $bdiSource"
}

$nemesisSource = Join-Path $PSScriptRoot 'Nemesis_Engine'
$nemesisDest = Join-Path $modRoot 'Nemesis_Engine'

if (Test-Path -LiteralPath $nemesisSource) {
    # Mirror the Nemesis patch so a regenerated patch fully replaces the old one.
    if (Test-Path -LiteralPath $nemesisDest) {
        Remove-Item -LiteralPath $nemesisDest -Recurse -Force
    }

    Copy-Item -LiteralPath $nemesisSource -Destination $nemesisDest -Recurse -Force

    $nemesisModDir = Join-Path $nemesisSource 'mod'
    $patchNames = if (Test-Path -LiteralPath $nemesisModDir) {
        (Get-ChildItem -LiteralPath $nemesisModDir -Directory | Select-Object -ExpandProperty Name) -join ', '
    } else { '(none)' }
    Write-Host "Copied Nemesis patch(es) [$patchNames] to $nemesisDest"
} else {
    Write-Host "Nemesis_Engine source directory not found: $nemesisSource"
}
