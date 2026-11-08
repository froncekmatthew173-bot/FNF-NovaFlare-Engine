[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MapFile,

    [Parameter(Mandatory = $true)]
    [string]$AnalyzerSummary,

    [Parameter(Mandatory = $true)]
    [string]$OutputFile,

    [Parameter(Mandatory = $true)]
    [double]$IntervalStartMs,

    [Parameter(Mandatory = $true)]
    [double]$IntervalEndMs,

    [int64]$ImageBase = 0x140000000,

    [ValidateRange(1, 1000)]
    [int]$Top = 200
)

$ErrorActionPreference = 'Stop'

$mapPath = [IO.Path]::GetFullPath($MapFile)
$summaryPath = [IO.Path]::GetFullPath($AnalyzerSummary)
$outputPath = [IO.Path]::GetFullPath($OutputFile)
foreach ($path in @($mapPath, $summaryPath)) {
    if (-not [IO.File]::Exists($path)) {
        throw "Input file not found: $path"
    }
}

$symbolsByAddress = [Collections.Generic.Dictionary[long,string]]::new()
$mapPattern = [Text.RegularExpressions.Regex]::new(
    '^\s*0x([0-9a-fA-F]{16})\s+(.+?)\s*$',
    [Text.RegularExpressions.RegexOptions]::Compiled)

foreach ($line in [IO.File]::ReadLines($mapPath)) {
    $match = $mapPattern.Match($line)
    if (-not $match.Success) { continue }

    $absolute = [Convert]::ToInt64($match.Groups[1].Value, 16)
    if ($absolute -lt $ImageBase) { continue }

    $symbol = $match.Groups[2].Value.Trim()
    if ($symbol.Length -eq 0 -or $symbol.StartsWith('0x') -or
        $symbol.StartsWith('*fill*')) {
        continue
    }

    $relative = $absolute - $ImageBase
    if (-not $symbolsByAddress.ContainsKey($relative) -or
        $symbol.Contains('::') -or $symbol.Contains('(')) {
        $symbolsByAddress[$relative] = $symbol
    }
}

$symbolAddresses = [long[]]@($symbolsByAddress.Keys | Sort-Object)
if ($symbolAddresses.Count -eq 0) {
    throw 'No image-relative symbols were parsed from the linker map.'
}

function Find-SymbolIndex([long]$Offset) {
    $low = 0
    $high = $symbolAddresses.Count - 1
    $best = -1
    while ($low -le $high) {
        $middle = $low + [Math]::Floor(($high - $low) / 2)
        $address = $symbolAddresses[$middle]
        if ($address -le $Offset) {
            $best = $middle
            $low = $middle + 1
        }
        else {
            $high = $middle - 1
        }
    }
    return $best
}

function Resolve-Offset([long]$Offset) {
    $index = Find-SymbolIndex $Offset
    if ($index -lt 0) {
        return [pscustomobject]@{
            Offset = "0x$($Offset.ToString('x'))"
            Delta = ''
            Symbol = '<unresolved>'
        }
    }
    $start = $symbolAddresses[$index]
    [pscustomobject]@{
        Offset = "0x$($Offset.ToString('x'))"
        Delta = "0x$(($Offset - $start).ToString('x'))"
        Symbol = $symbolsByAddress[$start]
    }
}

$intervalPattern = [Text.RegularExpressions.Regex]::new(
    '^samples=\d+\s+interval=([0-9.]+)-([0-9.]+)ms\s+pid=(\d+)$',
    [Text.RegularExpressions.RegexOptions]::Compiled)
$threadLeavesPattern = [Text.RegularExpressions.Regex]::new(
    '^THREAD_LEAVES TID=(\d+)$',
    [Text.RegularExpressions.RegexOptions]::Compiled)
$threadInclusivePattern = [Text.RegularExpressions.Regex]::new(
    '^THREAD_INCLUSIVE TID=(\d+)$',
    [Text.RegularExpressions.RegexOptions]::Compiled)
$threadEdgesPattern = [Text.RegularExpressions.Regex]::new(
    '^THREAD_CALLER_EDGES TID=(\d+)$',
    [Text.RegularExpressions.RegexOptions]::Compiled)
$engineLeafPattern = [Text.RegularExpressions.Regex]::new(
    '^\s*(\d+)\s+.*novaflare engine\.exe\+0x([0-9a-fA-F]+)\s*$',
    [Text.RegularExpressions.RegexOptions]::Compiled -bor
    [Text.RegularExpressions.RegexOptions]::IgnoreCase)
$engineEdgePattern = [Text.RegularExpressions.Regex]::new(
    '^\s*(\d+)\s+.*novaflare engine\.exe\+0x([0-9a-fA-F]+)\s+<-\s+(.+)$',
    [Text.RegularExpressions.RegexOptions]::Compiled -bor
    [Text.RegularExpressions.RegexOptions]::IgnoreCase)
$engineCallerPattern = [Text.RegularExpressions.Regex]::new(
    'novaflare engine\.exe\+0x([0-9a-fA-F]+)\s*$',
    [Text.RegularExpressions.RegexOptions]::Compiled -bor
    [Text.RegularExpressions.RegexOptions]::IgnoreCase)

$insideInterval = $false
$section = ''
$threadId = 0
$processId = 0
$records = [Collections.Generic.List[object]]::new()

foreach ($line in [IO.File]::ReadLines($summaryPath)) {
    $intervalMatch = $intervalPattern.Match($line)
    if ($intervalMatch.Success) {
        $start = [double]::Parse(
            $intervalMatch.Groups[1].Value,
            [Globalization.CultureInfo]::InvariantCulture)
        $end = [double]::Parse(
            $intervalMatch.Groups[2].Value,
            [Globalization.CultureInfo]::InvariantCulture)
        $insideInterval =
            [Math]::Abs($start - $IntervalStartMs) -lt 0.001 -and
            [Math]::Abs($end - $IntervalEndMs) -lt 0.001
        if ($insideInterval) {
            $processId = [int]$intervalMatch.Groups[3].Value
        }
        $section = ''
        $threadId = 0
        continue
    }
    if (-not $insideInterval) { continue }

    $threadMatch = $threadLeavesPattern.Match($line)
    if ($threadMatch.Success) {
        $section = 'thread_leaf'
        $threadId = [int]$threadMatch.Groups[1].Value
        continue
    }
    $threadMatch = $threadInclusivePattern.Match($line)
    if ($threadMatch.Success) {
        $section = 'thread_inclusive'
        $threadId = [int]$threadMatch.Groups[1].Value
        continue
    }
    $threadMatch = $threadEdgesPattern.Match($line)
    if ($threadMatch.Success) {
        $section = 'thread_edge'
        $threadId = [int]$threadMatch.Groups[1].Value
        continue
    }
    if ($line -eq 'LEAVES') {
        $section = 'global_leaf'
        $threadId = 0
        continue
    }
    if ($line -eq 'INCLUSIVE') {
        $section = 'global_inclusive'
        $threadId = 0
        continue
    }

    if ($section -eq 'thread_edge') {
        $match = $engineEdgePattern.Match($line)
        if (-not $match.Success) { continue }
        $offset = [Convert]::ToInt64($match.Groups[2].Value, 16)
        $resolved = Resolve-Offset $offset
        $callerText = $match.Groups[3].Value.Trim()
        $callerMatch = $engineCallerPattern.Match($callerText)
        $callerOffset = ''
        $callerDelta = ''
        $callerSymbol = $callerText
        if ($callerMatch.Success) {
            $caller = Resolve-Offset (
                [Convert]::ToInt64($callerMatch.Groups[1].Value, 16))
            $callerOffset = $caller.Offset
            $callerDelta = $caller.Delta
            $callerSymbol = $caller.Symbol
        }
        [void]$records.Add([pscustomobject]@{
            section = $section
            tid = $threadId
            samples = [long]$match.Groups[1].Value
            offset = $resolved.Offset
            delta = $resolved.Delta
            symbol = $resolved.Symbol
            caller_offset = $callerOffset
            caller_delta = $callerDelta
            caller_symbol = $callerSymbol
        })
        continue
    }

    if ($section -notin @(
        'thread_leaf', 'thread_inclusive',
        'global_leaf', 'global_inclusive')) {
        continue
    }
    $match = $engineLeafPattern.Match($line)
    if (-not $match.Success) { continue }
    $resolved = Resolve-Offset (
        [Convert]::ToInt64($match.Groups[2].Value, 16))
    [void]$records.Add([pscustomobject]@{
        section = $section
        tid = $threadId
        samples = [long]$match.Groups[1].Value
        offset = $resolved.Offset
        delta = $resolved.Delta
        symbol = $resolved.Symbol
        caller_offset = ''
        caller_delta = ''
        caller_symbol = ''
    })
}

if ($processId -eq 0) {
    throw "Interval $IntervalStartMs-$IntervalEndMs ms was not found."
}

$selected = @(
    $records |
        Group-Object section, tid |
        ForEach-Object {
            $_.Group | Sort-Object samples -Descending | Select-Object -First $Top
        }
)
[IO.Directory]::CreateDirectory(
    [IO.Path]::GetDirectoryName($outputPath)) | Out-Null
$selected | Export-Csv -LiteralPath $outputPath -Delimiter "`t" `
    -NoTypeInformation -Encoding utf8

[pscustomobject]@{
    Pid = $processId
    Interval = "$IntervalStartMs-$IntervalEndMs"
    Records = $selected.Count
    Output = $outputPath
}
