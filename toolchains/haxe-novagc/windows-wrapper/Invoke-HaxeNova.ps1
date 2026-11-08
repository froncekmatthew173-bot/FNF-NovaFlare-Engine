$ErrorActionPreference = 'Stop'

$toolchain = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$compiler = Join-Path $toolchain 'haxe.exe'
$standardLibrary = Join-Path $toolchain 'std'

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Local NovaGC Haxe compiler is missing: $compiler"
}
if (-not (Test-Path -LiteralPath $standardLibrary)) {
    throw "Local NovaGC Haxe standard library is missing: $standardLibrary"
}

$previousStdPath = $env:HAXE_STD_PATH
$previousPath = $env:PATH
try {
    # The compiler and its five MinGW runtime DLLs live together in the
    # toolchain directory.  Keep that directory first so this invocation is
    # independent from whichever stock Haxe installation is on the machine.
    $env:HAXE_STD_PATH = $standardLibrary
    $env:PATH = "$toolchain;$previousPath"
    & $compiler @args
    $compilerExit = $LASTEXITCODE
}
finally {
    $env:HAXE_STD_PATH = $previousStdPath
    $env:PATH = $previousPath
}

exit $compilerExit
