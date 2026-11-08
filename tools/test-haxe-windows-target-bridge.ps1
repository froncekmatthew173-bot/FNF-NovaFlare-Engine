[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$wrapper = Join-Path $root 'toolchains\haxe-novagc\windows-wrapper\haxe.cmd'
$hxml = Join-Path $root 'toolchains\haxe-novagc\tests\hxcpp_zgc_windows_target_probe\build.hxml'
$output = Join-Path $root '_build\hxcpp-zgc-windows-target-probe'

if (Test-Path -LiteralPath $output) {
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $root '_build')).TrimEnd('\') + '\'
    $resolvedOutput = [IO.Path]::GetFullPath($output).TrimEnd('\') + '\'
    if (-not $resolvedOutput.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace probe output outside _build: $output"
    }
    Remove-Item -LiteralPath $output -Recurse -Force
}

$oldStdPath = $env:HAXE_STD_PATH
try {
    $env:HAXE_STD_PATH = Join-Path $root 'toolchains\haxe-novagc\std'
    Push-Location $root
    try {
        & $wrapper $hxml
        if ($LASTEXITCODE -ne 0) {
            throw "Windows target bridge probe generation failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:HAXE_STD_PATH = $oldStdPath
}

$nativeSource = Join-Path $output 'src\trandom\Native.cpp'
$buildXml = Join-Path $output 'Build.xml'
$options = Join-Path $output 'Options.txt'
foreach ($required in @($nativeSource, $buildXml, $options)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Windows target bridge probe output is missing: $required"
    }
}

$nativeText = [IO.File]::ReadAllText($nativeSource)
$buildText = [IO.File]::ReadAllText($buildXml)
$optionText = [IO.File]::ReadAllText($options)
if ($nativeText -notmatch 'Native_obj::getWindows' -or
    $nativeText -notmatch 'trandom_get' -or
    $buildText -notmatch 'haxelib:trandom.+native.hxcpp_build\.xml' -or
    $optionText -notmatch '(?m)^trandom_windows=1\s*$') {
    throw 'Windows target bridge omitted the trandom native entropy branch.'
}

Write-Host 'Windows target/host bridge probe passed.'
