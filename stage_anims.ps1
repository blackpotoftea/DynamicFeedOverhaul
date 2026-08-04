[CmdletBinding()]
param(
    # Process just one ad-hoc set instead of the default list below.
    [string]$SourceDir,
    [string]$SetName,
    # Also print the FNIS_DynamicFeedOverhaul_List.txt lines for the copied clips.
    [switch]$FnisLines
)

$ErrorActionPreference = 'Stop'

# Pipeline FINAL output: SSE-format clips with the _1 rotation fix and _0 VFD_
# annotations baked in. This is the only stage that works in-game - the sibling
# 'hkx' folder is a pre-conversion intermediate and must NOT be used. Each set is
# named explicitly so the LE/32-bit variants (batch\_le_test, *_le) are never staged.
$AssetBatch = 'C:\Modding\blender_assets\coache_blender\ostim_devour_correct\batch'

# Each set: a source folder of finished .hkx + the meshes subfolder to fill.
if ($SourceDir) {
    if (-not $SetName) { throw '-SetName is required when -SourceDir is given.' }
    $sets = @(@{ Source = $SourceDir; Name = $SetName })
} else {
    $sets = @(
        @{ Source = (Join-Path $AssetBatch 'vampireFurnitureFeed');            Name = 'VampireFurnitureFeed' }
        @{ Source = (Join-Path $AssetBatch 'DevourVampireBiteMaleStanding');   Name = 'DevourVampireBiteMaleStanding' }
        @{ Source = (Join-Path $AssetBatch 'DevourVampireBiteFemaleStanding'); Name = 'DevourVampireBiteFemaleStanding' }
    )
}

$animRoot = Join-Path $PSScriptRoot 'meshes\actors\character\animations\DynamicFeedOverhaul'

# snake_case fragment -> PascalCase, for the furniture clips. Ordered so compound
# words (bedrollleft) are rewritten before their substrings. -replace is
# case-insensitive, which is fine since the source names are lower-case.
$map = [ordered]@{
    'vampire'      = 'Vampire'
    'feed'         = 'Feed'
    'bedrollleft'  = 'BedrollLeft'
    'bedrollright' = 'BedrollRight'
    'bedleft'      = 'BedLeft'
    'bedright'     = 'BedRight'
    'kneeling'     = 'Kneeling'
    'enter'        = 'Enter'
    'loop'         = 'Loop'
    'exit'         = 'Exit'
}

# Returns the DFO_ mod name, or $null for files that match no known convention
# (e.g. stray test.hkx) so they can be skipped.
function ConvertTo-DfoName([string]$baseName) {
    if ($baseName -match '^_Em-(.+)$') {
        # Blender export prefix -> mod prefix; the rest is already PascalCase + _0/_1.
        return "DFO_$($Matches[1])"
    }
    if ($baseName -match '^vampire_feed') {
        $n = $baseName.ToLower()
        foreach ($k in $map.Keys) { $n = $n -replace $k, $map[$k] }
        $n = $n -replace '_', ''
        return "DFO_$n"
    }
    return $null
}

$fnis = @()
$total = 0
foreach ($set in $sets) {
    if (-not (Test-Path -LiteralPath $set.Source)) {
        if ($SourceDir) { throw "Source directory not found: $($set.Source)" }
        Write-Warning "Skipping '$($set.Name)' - source not found: $($set.Source)"
        continue
    }

    $destDir = Join-Path $animRoot $set.Name
    if (-not (Test-Path -LiteralPath $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }

    Write-Host "[$($set.Name)]"
    Get-ChildItem -LiteralPath $set.Source -Filter '*.hkx' -File | Sort-Object Name | ForEach-Object {
        $newBase = ConvertTo-DfoName $_.BaseName
        if (-not $newBase) {
            Write-Warning "  skipped (unrecognized name): $($_.Name)"
            return
        }
        $newName = "$newBase.hkx"
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $destDir $newName) -Force
        Write-Host ("  {0,-46} -> {1}" -f $_.Name, $newName)
        # Transition clips (GoTo/GoBack, Enter/Exit) play once; everything else
        # (Devour/Drained loops, Loop) is cyclic. Mirrors the proven FNIS flags.
        $flags = if ($newBase -match 'GoTo|GoBack|Enter|Exit') { '-a,Tn' } else { '-Tn' }
        $fnis += ("b {0} {1} {2}\{3}" -f $flags, $newBase, $set.Name, $newName)
        $total++
    }
}

Write-Host "`nCopied $total clip(s)."

if ($FnisLines) {
    Write-Host "`n--- FNIS lines ---"
    $fnis | ForEach-Object { Write-Host $_ }
}
