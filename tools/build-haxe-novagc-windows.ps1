[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $root 'toolchains\haxe-novagc'
$opam = (Get-Command opam.exe -ErrorAction Stop).Source
$opamRoot = (& $opam var root).Trim()
$opamPrefix = (& $opam var prefix).Trim()
$opamBin = Join-Path $opamPrefix 'bin'
$cygwinRoot = Join-Path $opamRoot '.cygwin\root'
$cygwinBin = Join-Path $cygwinRoot 'bin'
$mingwBin = Join-Path $cygwinRoot 'usr\x86_64-w64-mingw32\sys-root\mingw\bin'
$make = Join-Path $cygwinBin 'make.exe'

foreach ($required in @($make, (Join-Path $opamBin 'dune.exe'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Local Haxe build dependency is missing: $required"
    }
}

$previousPath = $env:PATH
$previousCamlLibraryPath = $env:CAML_LD_LIBRARY_PATH
$previousSwitch = $env:OPAMSWITCH
$previousPrefix = $env:OPAM_SWITCH_PREFIX
try {
    $env:OPAMSWITCH = 'default'
    $env:OPAM_SWITCH_PREFIX = $opamPrefix
    $env:CAML_LD_LIBRARY_PATH = @(
        (Join-Path $opamPrefix 'lib\stublibs')
        (Join-Path $opamPrefix 'lib\ocaml\stublibs')
        (Join-Path $opamPrefix 'lib\ocaml')
    ) -join ';'
    $env:PATH = "$opamBin;$mingwBin;$cygwinBin;$previousPath"

    Push-Location $toolchain
    try {
        & $make -f Makefile.win ARCH=64 haxe
        if ($LASTEXITCODE -ne 0) {
            throw "Local NovaGC Haxe build failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:PATH = $previousPath
    $env:CAML_LD_LIBRARY_PATH = $previousCamlLibraryPath
    $env:OPAMSWITCH = $previousSwitch
    $env:OPAM_SWITCH_PREFIX = $previousPrefix
}

$runtimeDlls = @(
    'libmbedcrypto.dll'
    'libmbedtls.dll'
    'libmbedx509.dll'
    'libpcre2-8-0.dll'
    'zlib1.dll'
)
foreach ($name in $runtimeDlls) {
    $source = Join-Path $mingwBin $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Local NovaGC Haxe runtime dependency is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $toolchain $name) -Force
}

$compiler = Join-Path $toolchain 'haxe.exe'
& $compiler --version
if ($LASTEXITCODE -ne 0) {
    throw "Local NovaGC Haxe compiler did not start (exit $LASTEXITCODE)"
}

Write-Host "Local NovaGC Haxe compiler is ready: $compiler"
