[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repo '_build'))
$buildPrefix = $buildRoot.TrimEnd('\') + '\'
$removedBytes = [uint64]0

function Resolve-BuildTarget {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $resolved = [IO.Path]::GetFullPath((Join-Path $repo $RelativePath))
    if (-not $resolved.StartsWith(
        $buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target escapes _build: $resolved"
    }
    return $resolved
}

$directoryTargets = @(
    '_build\diagnostic-pipeline-smoke-current-exe-20260723',
    '_build\diagnostic-pipeline-etw-smoke-current-exe-20260723',
    '_build\diagnostic-pipeline-etw-smoke2-current-exe-20260723'
)

$fileTargets = @(
    '_build\next-stage-mainmenu-diagnostics-180s-20260723\cpu-stacks.etl',
    '_build\next-stage-mainmenu-diagnostics-180s-20260723\novaflare-symbols.map',
    '_build\next-stage-mainmenu-diagnostics-180s-20260723\symbol-layout.exe',
    '_build\next-stage-mainmenu-diagnostics-180s-20260723\symbol-link.rsp'
)

foreach ($relative in $directoryTargets) {
    $target = Resolve-BuildTarget $relative
    if (-not (Test-Path -LiteralPath $target -PathType Container)) {
        continue
    }
    $size = (Get-ChildItem -LiteralPath $target -File -Recurse |
        Measure-Object Length -Sum).Sum
    if ($null -ne $size) {
        $removedBytes += [uint64]$size
    }
    Remove-Item -LiteralPath $target -Recurse -Force
    Write-Output "removed_directory=$target"
}

foreach ($relative in $fileTargets) {
    $target = Resolve-BuildTarget $relative
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        continue
    }
    $removedBytes += [uint64](Get-Item -LiteralPath $target).Length
    Remove-Item -LiteralPath $target -Force
    Write-Output "removed_file=$target"
}

$temporaryEtlx =
    'C:\Users\Administrator\AppData\Local\Temp\PerfView\cpu-stacks_51769da2.etlx'
$expectedTemporaryEtlx = [IO.Path]::GetFullPath($temporaryEtlx)
if (Test-Path -LiteralPath $expectedTemporaryEtlx -PathType Leaf) {
    $resolvedTemporaryEtlx =
        (Resolve-Path -LiteralPath $expectedTemporaryEtlx).Path
    if (-not [string]::Equals(
        $resolvedTemporaryEtlx, $expectedTemporaryEtlx,
        [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected PerfView cache path: $resolvedTemporaryEtlx"
    }
    $removedBytes +=
        [uint64](Get-Item -LiteralPath $resolvedTemporaryEtlx).Length
    Remove-Item -LiteralPath $resolvedTemporaryEtlx -Force
    Write-Output "removed_file=$resolvedTemporaryEtlx"
}

Write-Output "removed_bytes=$removedBytes"
Write-Output "removed_gib=$([Math]::Round($removedBytes / 1GB, 3))"
