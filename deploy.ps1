[CmdletBinding()]
param(
    [switch]$NV
)

$ErrorActionPreference = 'Stop'

$source = Join-Path $PSScriptRoot 'build\relwithdebinfo-msvc\RelWithDebInfo'

if ($NV) {
    $destination = 'D:\wabjack\skyrim\nv\MODS\mods\DynamicFeedOverhaul\SKSE\Plugins'
} else {
    $destination = 'D:\mods\ModOrganizer\Portable_Skyrim_Special_Edition\mods\DynamicFeedOverhaul\SKSE\Plugins'
}

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
