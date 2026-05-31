[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$buildDir = Join-Path $PSScriptRoot 'build\relwithdebinfo-msvc'

if (-not (Test-Path -LiteralPath $buildDir)) {
    throw "Build directory not found: $buildDir"
}

& cmake --build $buildDir --config RelWithDebInfo
if ($LASTEXITCODE -ne 0) {
    throw "cmake build failed with exit code $LASTEXITCODE"
}
