$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$generated = Join-Path $root 'toolchains\haxe-novagc\_build\hxcpp-zgc-unit-current'
$buildFile = Join-Path $generated 'Build.xml'
$kernelBuild = Join-Path $root '_build\hxcpp-zgc-kernel'
$output = Join-Path $kernelBuild 'hxcpp-generated-haxe-unit.exe'
$runtimeLog = Join-Path $kernelBuild 'haxe-unit-runtime.log'
$cmakeSource = Join-Path $kernelBuild 'hxcpp-generated-haxe-unit-cmake'
$cmakeBuild = Join-Path $kernelBuild 'hxcpp-generated-haxe-unit-build'

if (-not (Test-Path -LiteralPath $buildFile)) {
    throw "Generated Haxe unit output is missing: $buildFile"
}

[xml]$build = Get-Content -LiteralPath $buildFile
$sources = @(
    $build.SelectNodes("/xml/files[@id='haxe' or @id='__resources__']/file[not(@if)]") |
        ForEach-Object { $_.GetAttribute('name') } |
        Where-Object {
            [IO.Path]::GetExtension($_) -eq '.cpp' -and
            [IO.Path]::GetFileNameWithoutExtension($_) -ne '__files__'
        } |
        ForEach-Object { Join-Path $generated $_ }
)

function ConvertTo-CMakePath([string]$value) {
    return $value.Replace('\', '/').Replace('"', '\"')
}

New-Item -ItemType Directory -Force -Path $cmakeSource | Out-Null
$sourceLines = @($sources) +
    (Join-Path $root 'hxcpp\tests\generated_unit_main.cpp') |
    ForEach-Object { '    "' + (ConvertTo-CMakePath $_) + '"' }
$cmakeContents = @"
cmake_minimum_required(VERSION 3.24)
project(hxcpp_generated_haxe_unit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(hxcpp-generated-haxe-unit
$($sourceLines -join "`n")
)
target_include_directories(hxcpp-generated-haxe-unit PRIVATE
    "$(ConvertTo-CMakePath (Join-Path $root 'hxcpp\include'))"
    "$(ConvertTo-CMakePath (Join-Path $generated 'include'))"
)
target_compile_options(hxcpp-generated-haxe-unit PRIVATE
    -O0 -g0 -Wall -Wextra -Wpedantic
    -Wno-overloaded-virtual -Wno-unused-variable -Wno-unused-result
    -fmax-errors=200
)
target_link_libraries(hxcpp-generated-haxe-unit PRIVATE
    "$(ConvertTo-CMakePath (Join-Path $kernelBuild 'libhxcpp-runtime.a'))"
    z pcre2-16 mbedtls mbedx509 mbedcrypto ws2_32 crypt32 bcrypt
)
target_link_options(hxcpp-generated-haxe-unit PRIVATE
    -static -static-libgcc -static-libstdc++
)
set_target_properties(hxcpp-generated-haxe-unit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$(ConvertTo-CMakePath $kernelBuild)"
)
"@
$cmakeFile = Join-Path $cmakeSource 'CMakeLists.txt'
if (-not (Test-Path -LiteralPath $cmakeFile) -or
    [IO.File]::ReadAllText($cmakeFile) -ne $cmakeContents) {
    [IO.File]::WriteAllText($cmakeFile, $cmakeContents,
                            [Text.UTF8Encoding]::new($false))
}

$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
& cmake -S $cmakeSource -B $cmakeBuild -G Ninja `
    '-DCMAKE_BUILD_TYPE=Release' `
    '-DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe'
if ($LASTEXITCODE -ne 0) {
    throw "Generated Haxe unit CMake configuration failed with exit code $LASTEXITCODE"
}

# Ninja's keep-going mode compiles every independent translation unit even
# after failures.  This turns one run into a complete compatibility audit and
# preserves successful objects for the next incremental repair pass.
& cmake --build $cmakeBuild --parallel 8 -- -k 1000
if ($LASTEXITCODE -ne 0) {
    throw "Generated Haxe unit parallel compilation failed with exit code $LASTEXITCODE"
}

$savedErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    & $output 2>&1 | Tee-Object -FilePath $runtimeLog
    $unitExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $savedErrorActionPreference
}
if ($unitExitCode -ne 0) {
    throw "Generated Haxe unit suite failed with exit code $unitExitCode (log: $runtimeLog)"
}
