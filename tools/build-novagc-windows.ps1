param(
    [ValidateSet('build', 'test')]
    [string]$Command = 'build'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $root 'toolchains\haxe-novagc'
$haxeShim = Join-Path $toolchain 'windows-wrapper'
$lime = (Get-Command lime.exe -ErrorAction Stop).Source
$mingw = 'C:\msys64\mingw64\bin'

if (!(Test-Path -LiteralPath (Join-Path $haxeShim 'haxe.cmd'))) {
    throw "NovaGC Haxe shim is missing: $haxeShim\haxe.cmd"
}
if (!(Test-Path -LiteralPath (Join-Path $toolchain 'std'))) {
    throw "NovaGC standard library is missing: $toolchain\std"
}

$previousPath = $env:PATH
$previousStdPath = $env:HAXE_STD_PATH
try {
    $runtimePath = if (Test-Path -LiteralPath $mingw) { "$mingw;$previousPath" } else { $previousPath }
    $env:PATH = "$haxeShim;$runtimePath"
    $env:HAXE_STD_PATH = Join-Path $toolchain 'std'
    Push-Location $root
    try {
        # Windows PowerShell converts native stderr records (including Haxe's
        # ordinary WARNING diagnostics) into PowerShell errors.  With the
        # script-wide Stop policy that can abort before LASTEXITCODE is checked,
        # especially when callers redirect stdout/stderr for GC test logs.
        # Native tool success is defined by its process exit code instead.
        $savedErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & $lime $Command windows
            $limeExitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $savedErrorActionPreference
        }
        if ($limeExitCode -ne 0) {
            throw "lime $Command windows failed with exit code $limeExitCode"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:PATH = $previousPath
    $env:HAXE_STD_PATH = $previousStdPath
}

$probe = Join-Path $root 'export\release\windows\obj\include\flixel\FlxSprite.h'
$descriptorProbe = Join-Path $root 'export\release\windows\obj\src\flixel\FlxSprite.cpp'
if (!(Test-Path -LiteralPath $probe)) {
    throw "Generated ABI probe is missing: $probe"
}
$generated = Get-Content -LiteralPath $probe -Raw
if ($generated -notmatch '__novaType' -or
    $generated -notmatch 'Runtime::instance\(\)\.allocate\(__novaType\)' -or
    $generated -notmatch 'HX_NOVAGC_WB') {
    throw 'Generated output is not using the NovaGC precise ABI. Refusing the build.'
}
if (!(Test-Path -LiteralPath $descriptorProbe)) {
    throw "Generated descriptor probe is missing: $descriptorProbe"
}
$descriptor = Get-Content -LiteralPath $descriptorProbe -Raw
if ($descriptor -notmatch 'sNovaPointerOffsets' -or
    $descriptor -notmatch '::hx::gc::TypeContainsReferences' -or
    $descriptor -notmatch '__novaCffiGet' -or
    $descriptor -match '::hx::novagc::') {
    throw 'Generated output is missing the hxcpp-zgc exact descriptor metadata or still contains the retired NovaGC ABI. Refusing the build.'
}

Write-Host 'NovaGC precise ABI verification passed.'
