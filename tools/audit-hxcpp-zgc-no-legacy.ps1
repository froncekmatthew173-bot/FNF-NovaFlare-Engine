$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$runtime = Join-Path $workspace 'hxcpp'

if (-not (Test-Path -LiteralPath $runtime)) {
    throw 'New hxcpp package is missing'
}

$forbidden = @(
    'Immix',
    'LegacyBridge',
    'MarkConservative',
    'MarkAllocUnchecked',
    'NewGCBytes',
    'InternalNew',
    'HX_OBJ_WB',
    'DoMarkThis',
    'DoVisitThis',
    '\.haxelib[\\/]hxcpp',
    'include[\\/]hx[\\/]GC\.h'
)

$violations = @()
foreach ($pattern in $forbidden) {
    $result = & rg --line-number --hidden --glob '!README.md' --regexp $pattern $runtime 2>$null
    if ($LASTEXITCODE -eq 0) {
        $violations += $result
    } elseif ($LASTEXITCODE -ne 1) {
        throw "rg failed while auditing pattern: $pattern"
    }
}

if ($violations.Count -ne 0) {
    $violations | ForEach-Object { Write-Error $_ }
    throw 'New hxcpp package contains a forbidden legacy dependency or symbol'
}

Write-Output 'hxcpp single-heap no-legacy audit passed.'
