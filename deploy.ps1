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
