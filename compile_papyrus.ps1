<#
.SYNOPSIS
    Compile the mod's Papyrus source (Scripts\Source\*.psc) to Scripts\*.pex.

.DESCRIPTION
    The DynamicFeedOverhaul script is a tiny detection header (native globals
    registered by the DLL). The game loads the compiled .pex, so it must be
    built and shipped - the .psc source alone is not enough at runtime.

    Locates PapyrusCompiler.exe and the base-game flags file under the Skyrim
    install (via -SkyrimFolder or the SKYRIM_FOLDER env var). Compiles in place;
    the resulting Scripts\DynamicFeedOverhaul.pex is what deploy.ps1/package.ps1
    pick up. Commit the .pex so a clean checkout can deploy without the CK.
#>
[CmdletBinding()]
param(
    # Skyrim SE install root (the folder containing SkyrimSE.exe). Defaults to
    # the SKYRIM_FOLDER env var that CMakeLists.txt already uses.
    [string]$SkyrimFolder = $env:SKYRIM_FOLDER
)

$ErrorActionPreference = 'Stop'

if (-not $SkyrimFolder) {
    throw "Set -SkyrimFolder or the SKYRIM_FOLDER environment variable to your Skyrim SE install root."
}
if (-not (Test-Path -LiteralPath $SkyrimFolder)) {
    throw "Skyrim folder not found: $SkyrimFolder"
}

$compiler = Join-Path $SkyrimFolder 'Papyrus Compiler\PapyrusCompiler.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    throw "PapyrusCompiler.exe not found: $compiler`nInstall the Creation Kit (it ships the compiler)."
}

# The flags file lives with the base-game script sources. SSE default is
# Data\Source\Scripts; older layouts use Data\Scripts\Source. Try both.
$baseSourceCandidates = @(
    (Join-Path $SkyrimFolder 'Data\Source\Scripts'),
    (Join-Path $SkyrimFolder 'Data\Scripts\Source')
)
$baseSource = $baseSourceCandidates | Where-Object {
    Test-Path -LiteralPath (Join-Path $_ 'TESV_Papyrus_Flags.flg')
} | Select-Object -First 1

if (-not $baseSource) {
    throw ("TESV_Papyrus_Flags.flg not found under:`n  " +
        ($baseSourceCandidates -join "`n  ") +
        "`nUnpack the base-game Scripts.zip / Source BSA so the flags file is present.")
}

$srcDir = Join-Path $PSScriptRoot 'Scripts\Source'
$outDir = Join-Path $PSScriptRoot 'Scripts'
$script = 'DynamicFeedOverhaul.psc'

if (-not (Test-Path -LiteralPath (Join-Path $srcDir $script))) {
    throw "Source script not found: $(Join-Path $srcDir $script)"
}
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

# -i import paths: our own source first, then the base-game source (for the flags
# file). The script imports nothing, so base sources need not be fully unpacked -
# only the .flg must resolve.
& $compiler (Join-Path $srcDir $script) `
    "-f=TESV_Papyrus_Flags.flg" `
    "-i=$srcDir;$baseSource" `
    "-o=$outDir"

if ($LASTEXITCODE -ne 0) {
    throw "PapyrusCompiler failed with exit code $LASTEXITCODE"
}

$pex = Join-Path $outDir 'DynamicFeedOverhaul.pex'
if (-not (Test-Path -LiteralPath $pex)) {
    throw "Compile reported success but $pex is missing."
}
Write-Host "Compiled $pex"
