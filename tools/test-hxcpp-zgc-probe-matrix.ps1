param(
    [switch]$SkipGeneration
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $root 'toolchains\haxe-novagc'
$matrixRoot = Join-Path $toolchain '_build\hxcpp-zgc-probe-matrix'
$kernelBuild = Join-Path $root '_build\hxcpp-zgc-kernel'
$outputRoot = Join-Path $kernelBuild 'probes'
$probeNames = @(
    'hxcpp_zgc_array_probe',
    'hxcpp_zgc_closure_probe',
    'hxcpp_zgc_enum_probe',
    'hxcpp_zgc_eventloop_probe',
    'hxcpp_zgc_exception_probe',
    'hxcpp_zgc_interface_dynamic_probe',
    'hxcpp_zgc_lime_cffi_probe',
    'hxcpp_zgc_prime_probe',
    'hxcpp_zgc_thread_probe'
)

New-Item -ItemType Directory -Path $matrixRoot -Force | Out-Null
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

if (-not $SkipGeneration) {
    docker start haxe-novagc-build | Out-Null
    # lime.system.CFFI lives in the project haxelib store, which is outside
    # this compiler container's deliberately narrow /src mount.  Its checked
    # in generated probe is still compiled below; the full Lime regeneration
    # is covered by build-novagc-windows.ps1.
    $compilerOnlyProbes = $probeNames | Where-Object {
        $_ -ne 'hxcpp_zgc_lime_cffi_probe'
    }
    $quotedNames = ($compilerOnlyProbes | ForEach-Object { "'$_'" }) -join ' '
    $generate = @"
eval `$(opam env)
for probe in $quotedNames; do
  ./haxe -cp "tests/`$probe" -main Main \
    -cpp "_build/hxcpp-zgc-probe-matrix/`$probe" \
    -D hxcpp_zgc -D no-compilation || exit `$?
done
"@
    docker exec haxe-novagc-build bash -lc $generate
    if ($LASTEXITCODE -ne 0) {
        throw "Haxe probe-matrix generation failed with exit code $LASTEXITCODE"
    }
}

$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$compiler = 'C:\msys64\mingw64\bin\g++.exe'
$probeMain = Join-Path $root 'hxcpp\tests\generated_probe_main.cpp'
$primeNative = Join-Path $root 'hxcpp\tests\generated_prime_native.cpp'
$runtime = Join-Path $kernelBuild 'libhxcpp-runtime.a'

foreach ($probe in $probeNames) {
    $generated = Join-Path $matrixRoot $probe
    if ($probe -eq 'hxcpp_zgc_lime_cffi_probe' -and
        -not (Test-Path -LiteralPath (Join-Path $generated 'Build.xml'))) {
        $generated = Join-Path $toolchain '_build\hxcpp-zgc-lime-cffi-probe'
    }
    $buildFile = Join-Path $generated 'Build.xml'
    if (-not (Test-Path -LiteralPath $buildFile)) {
        throw "Generated probe output is missing: $buildFile"
    }

    $legacy = rg -n 'hx::novagc|novazgc|Immix|LegacyBridge|Conservative|__Mark|__Visit|_hx_vtable' `
        (Join-Path $generated 'include') (Join-Path $generated 'src')
    if ($LASTEXITCODE -eq 0) {
        throw "Generated probe $probe contains forbidden legacy symbols:`n$legacy"
    }
    if ($LASTEXITCODE -gt 1) {
        throw "Generated source audit failed for $probe with exit code $LASTEXITCODE"
    }

    [xml]$build = Get-Content -LiteralPath $buildFile
    $sources = @(
        $build.SelectNodes("/xml/files[@id='haxe']/file[not(@if)]") |
            ForEach-Object { $_.GetAttribute('name') } |
            Where-Object {
                [IO.Path]::GetExtension($_) -eq '.cpp' -and
                ([IO.Path]::GetFileNameWithoutExtension($_) -notlike '__*' -or
                 [IO.Path]::GetFileNameWithoutExtension($_) -eq '__boot__')
            } |
            ForEach-Object { Join-Path $generated $_ }
    )
    $extraSources = @()
    if ($probe -in @('hxcpp_zgc_lime_cffi_probe',
                     'hxcpp_zgc_prime_probe')) {
        $extraSources += $primeNative
    }
    $output = Join-Path $outputRoot "$probe.exe"
    & $compiler -std=c++20 -Wall -Wextra -Wpedantic -Werror `
        -Wno-overloaded-virtual -Wno-unused-variable `
        -I (Join-Path $root 'hxcpp\include') `
        -I (Join-Path $generated 'include') `
        @sources @extraSources $probeMain $runtime `
        -lz -lpcre2-16 -lmbedtls -lmbedx509 -lmbedcrypto `
        -lws2_32 -lcrypt32 -lbcrypt `
        -static -static-libgcc -static-libstdc++ -o $output
    if ($LASTEXITCODE -ne 0) {
        throw "C++ compilation failed for $probe with exit code $LASTEXITCODE"
    }

    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "Probe $probe failed with exit code $LASTEXITCODE"
    }
    Write-Host "PASS $probe"
}

Write-Host "All $($probeNames.Count) generated Haxe probes passed"
