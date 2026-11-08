param(
    [switch]$SkipGeneration
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $root "toolchains/haxe-novagc"
$generated = Join-Path $toolchain "_build/hxcpp-zgc-smoke-current"
$kernelBuild = Join-Path $root "_build/hxcpp-zgc-kernel"
$output = Join-Path $kernelBuild "hxcpp-generated-haxe-smoke.exe"

if (-not $SkipGeneration) {
    docker start haxe-novagc-build | Out-Null
    docker exec haxe-novagc-build bash -lc `
        'eval $(opam env) && make haxe && ./haxe -cp tests/hxcpp_zgc_smoke -main Main -cpp _build/hxcpp-zgc-smoke-current -D hxcpp_zgc -D no-compilation'
    if ($LASTEXITCODE -ne 0) {
        throw "Haxe C++ generation failed with exit code $LASTEXITCODE"
    }
} elseif (-not (Test-Path -LiteralPath (Join-Path $generated "Build.xml"))) {
    throw "Generated Haxe smoke output is missing: $generated"
}

$legacyPatterns = 'hx::novagc|novazgc|Immix|LegacyBridge|Conservative|__Mark|__Visit|_hx_vtable'
$legacyMatches = rg -n $legacyPatterns `
    (Join-Path $generated "include/Main.h") `
    (Join-Path $generated "include/Node.h") `
    (Join-Path $generated "include/BaseNode.h") `
    (Join-Path $generated "src/Main.cpp") `
    (Join-Path $generated "src/Node.cpp") `
    (Join-Path $generated "src/BaseNode.cpp")
if ($LASTEXITCODE -eq 0) {
    throw "Generated Haxe smoke output contains forbidden legacy symbols:`n$legacyMatches"
}
if ($LASTEXITCODE -gt 1) {
    throw "Generated source audit failed with exit code $LASTEXITCODE"
}

$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
# Haxe can emit helper/native source files that are intentionally absent from
# Build.xml because hxcpp supplies their ABI (haxe.NativeStackTrace is one such
# class).  Compiling every physical *.cpp silently tests a different program
# than Lime/hxcpp.  Use the authoritative haxe fileset and keep the external
# smoke-test main used below.
[xml]$generatedBuild = Get-Content -LiteralPath (Join-Path $generated "Build.xml")
$generatedSources = @(
    $generatedBuild.SelectNodes("/xml/files[@id='haxe']/file[not(@if)]") |
        ForEach-Object { $_.GetAttribute("name") } |
        Where-Object {
            [IO.Path]::GetExtension($_) -eq ".cpp" -and
            ([IO.Path]::GetFileNameWithoutExtension($_) -notlike "__*" -or
             [IO.Path]::GetFileNameWithoutExtension($_) -eq "__boot__")
        } |
        ForEach-Object { Join-Path $generated $_ }
)
& "C:\msys64\mingw64\bin\g++.exe" `
    -std=c++20 -Wall -Wextra -Wpedantic -Werror `
    -Wno-overloaded-virtual -Wno-unused-variable `
    -I (Join-Path $root "hxcpp/include") `
    -I (Join-Path $generated "include") `
    @generatedSources `
    (Join-Path $root "hxcpp/tests/generated_haxe_smoke_main.cpp") `
    (Join-Path $kernelBuild "libhxcpp-runtime.a") `
    -lz -lpcre2-16 -lmbedtls -lmbedx509 -lmbedcrypto `
    -lws2_32 -lcrypt32 -lbcrypt `
    -static -static-libgcc -static-libstdc++ `
    -o $output
if ($LASTEXITCODE -ne 0) {
    throw "Generated Haxe C++ compilation failed with exit code $LASTEXITCODE"
}

& $output
if ($LASTEXITCODE -ne 0) {
    throw "Generated Haxe executable failed with exit code $LASTEXITCODE"
}
