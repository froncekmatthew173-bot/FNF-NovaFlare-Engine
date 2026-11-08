param(
    [ValidateSet('build', 'test')]
    [string]$Action = 'test',
    [switch]$ForceFresh
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = Join-Path $workspace 'hxcpp'
$build = Join-Path $workspace '_build\hxcpp-zgc-kernel'
$compiler = 'C:\msys64\mingw64\bin\g++.exe'
$compilerBin = Split-Path -Parent $compiler

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Required C++20 compiler was not found: $compiler"
}

# GCC launches cc1plus and the linker by name, so its own bin directory must
# be present even when CMake receives an absolute compiler path.
$env:PATH = "$compilerBin;$env:PATH"

$configureArgs = @(
    '-S', $source,
    '-B', $build,
    '-G', 'Ninja',
    "-DCMAKE_CXX_COMPILER=$compiler",
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo'
)
if ($ForceFresh) {
    $configureArgs = @('--fresh') + $configureArgs
}

# Preserve CMake/Ninja's dependency graph by default.  --fresh deletes the
# cache and made every validation run rebuild all 99 targets even when no
# source changed; use -ForceFresh only for an intentional toolchain reset.
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'Nova hxcpp kernel build failed' }

if ($Action -eq 'test') {
    ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Nova hxcpp kernel tests failed' }
}
