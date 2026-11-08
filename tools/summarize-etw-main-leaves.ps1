[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Analyzer,

    [Parameter(Mandatory = $true)]
    [string]$Trace,

    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [Parameter(Mandatory = $true)]
    [int]$ThreadId,

    [Parameter(Mandatory = $true)]
    [double]$StartMs,

    [Parameter(Mandatory = $true)]
    [double]$EndMs,

    [string]$KnownSymbolTable,

    [string]$MapFile,

    [string]$SymbolExecutable,

    [string]$Nm = 'nm',

    [ValidateRange(1, 1000)]
    [int]$MaximumRows = 80
)

$ErrorActionPreference = 'Stop'

function Convert-Hex([string]$Value) {
    return [Convert]::ToUInt64($Value.Trim().Substring(2), 16)
}

$symbolSourceCount = @(
    $KnownSymbolTable, $MapFile, $SymbolExecutable |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
).Count
if ($symbolSourceCount -ne 1) {
    throw (
        'Specify exactly one of -KnownSymbolTable, -MapFile, or ' +
        '-SymbolExecutable.')
}

if (-not [string]::IsNullOrWhiteSpace($KnownSymbolTable)) {
    $knownSymbols = @(
        Import-Csv -Delimiter "`t" -LiteralPath $KnownSymbolTable |
            ForEach-Object {
                $address = Convert-Hex $_.relative_address
                $delta = Convert-Hex $_.delta
                [pscustomobject]@{
                    Start = [uint64]($address - $delta)
                    Symbol = $_.symbol
                }
            } |
            Sort-Object Start, Symbol -Unique
    )
}
elseif (-not [string]::IsNullOrWhiteSpace($MapFile)) {
    $imageBase = [uint64]0x140000000
    $symbolsByAddress =
        [Collections.Generic.Dictionary[uint64,string]]::new()
    $mapPattern = [Text.RegularExpressions.Regex]::new(
        '^\s*0x([0-9a-fA-F]{16})\s+(.+?)\s*$',
        [Text.RegularExpressions.RegexOptions]::Compiled)
    foreach ($line in [IO.File]::ReadLines(
            [IO.Path]::GetFullPath($MapFile))) {
        $match = $mapPattern.Match($line)
        if (-not $match.Success) {
            continue
        }
        $absolute = [Convert]::ToUInt64($match.Groups[1].Value, 16)
        if ($absolute -lt $imageBase) {
            continue
        }
        $symbol = $match.Groups[2].Value.Trim()
        if ($symbol.Length -eq 0 -or $symbol.StartsWith('0x') -or
            $symbol.StartsWith('*fill*')) {
            continue
        }
        $relative = [uint64]($absolute - $imageBase)
        if (-not $symbolsByAddress.ContainsKey($relative) -or
            $symbol.Contains('::') -or $symbol.Contains('(')) {
            $symbolsByAddress[$relative] = $symbol
        }
    }
    $knownSymbols = @(
        $symbolsByAddress.GetEnumerator() |
            ForEach-Object {
                [pscustomobject]@{
                    Start = $_.Key
                    Symbol = $_.Value
                }
            } |
            Sort-Object Start
    )
}
else {
    $imageBase = [uint64]0x140000000
    $symbolsByAddress =
        [Collections.Generic.Dictionary[uint64,string]]::new()
    $nmPattern = [Text.RegularExpressions.Regex]::new(
        '^([0-9a-fA-F]{16})\s+[TtWw]\s+(.+?)\s*$',
        [Text.RegularExpressions.RegexOptions]::Compiled)
    & $Nm -n -C --defined-only (
        [IO.Path]::GetFullPath($SymbolExecutable)) |
        ForEach-Object {
            $match = $nmPattern.Match([string]$_)
            if (-not $match.Success) {
                return
            }
            $absolute = [Convert]::ToUInt64(
                $match.Groups[1].Value, 16)
            if ($absolute -lt $imageBase) {
                return
            }
            $relative = [uint64]($absolute - $imageBase)
            $symbol = $match.Groups[2].Value.Trim()
            if (-not $symbolsByAddress.ContainsKey($relative) -or
                $symbol.Contains('::') -or $symbol.Contains('(')) {
                $symbolsByAddress[$relative] = $symbol
            }
        }
    if ($LASTEXITCODE -ne 0) {
        throw "nm failed with exit code $LASTEXITCODE."
    }
    $knownSymbols = @(
        $symbolsByAddress.GetEnumerator() |
            ForEach-Object {
                [pscustomobject]@{
                    Start = $_.Key
                    Symbol = $_.Value
                }
            } |
            Sort-Object Start
    )
}

if ($knownSymbols.Count -eq 0) {
    throw 'No symbols were loaded.'
}

function Resolve-Symbol([uint64]$Address) {
    $low = 0
    $high = $knownSymbols.Count - 1
    $best = $null
    while ($low -le $high) {
        $middle = [int](($low + $high) / 2)
        $candidate = $knownSymbols[$middle]
        if ($candidate.Start -le $Address) {
            $best = $candidate
            $low = $middle + 1
        }
        else {
            $high = $middle - 1
        }
    }
    if ($null -eq $best) {
        return [pscustomobject]@{
            Symbol = '<unresolved>'
            Delta = $Address
        }
    }
    return [pscustomobject]@{
        Symbol = $best.Symbol
        Delta = $Address - $best.Start
    }
}

$captureLeaves = $false
$captureEdges = $false
$threadSamples = 0
$leafRows = [Collections.Generic.List[object]]::new()
$edgeRows = [Collections.Generic.List[object]]::new()
$inThreads = $false

& $Analyzer $Trace $ProcessId $StartMs $EndMs |
    ForEach-Object {
        $line = [string]$_
        if ($line -eq 'THREADS') {
            $inThreads = $true
            return
        }
        if ($inThreads -and $line -match '^([0-9]+)\s+TID=([0-9]+)$') {
            if ([int]$Matches[2] -eq $ThreadId) {
                $threadSamples = [int]$Matches[1]
            }
            return
        }
        if ($line -eq "THREAD_LEAVES TID=$ThreadId") {
            $captureLeaves = $true
            $captureEdges = $false
            $inThreads = $false
            return
        }
        if ($captureLeaves -and $line -eq "THREAD_INCLUSIVE TID=$ThreadId") {
            $captureLeaves = $false
            return
        }
        if ($line -eq "THREAD_CALLER_EDGES TID=$ThreadId") {
            $captureEdges = $true
            return
        }
        if (
            $captureEdges -and
            ($line -match '^THREAD_LEAVES TID=' -or $line -eq 'LEAVES')
        ) {
            $captureEdges = $false
        }
        if (
            $captureLeaves -and
            $line -match (
                '^([0-9]+)\s+.*novaflare engine\.exe\+0x([0-9a-f]+)$')
        ) {
            $address = [Convert]::ToUInt64($Matches[2], 16)
            $resolved = Resolve-Symbol $address
            $leafRows.Add([pscustomobject]@{
                    Samples = [int]$Matches[1]
                    Address = $address
                    Symbol = $resolved.Symbol
                    Delta = $resolved.Delta
                })
        }
        if (
            $captureEdges -and
            $line -match (
                '^([0-9]+)\s+.*novaflare engine\.exe\+0x([0-9a-f]+)' +
                ' <- .*novaflare engine\.exe\+0x([0-9a-f]+)$')
        ) {
            $leafAddress = [Convert]::ToUInt64($Matches[2], 16)
            $callerAddress = [Convert]::ToUInt64($Matches[3], 16)
            $leafResolved = Resolve-Symbol $leafAddress
            $callerResolved = Resolve-Symbol $callerAddress
            $edgeRows.Add([pscustomobject]@{
                    Samples = [int]$Matches[1]
                    Leaf = $leafResolved.Symbol
                    Caller = $callerResolved.Symbol
                })
        }
    }

$grouped = @(
    $leafRows |
        Group-Object Symbol |
        ForEach-Object {
            $samples = (
                $_.Group |
                    Measure-Object Samples -Sum
            ).Sum
            [pscustomobject]@{
                Samples = $samples
                Percent = if ($threadSamples -gt 0) {
                    [math]::Round($samples * 100.0 / $threadSamples, 3)
                }
                else {
                    0
                }
                Symbol = $_.Name
                MaximumDelta = (
                    $_.Group |
                        Measure-Object Delta -Maximum
                ).Maximum
            }
        } |
        Sort-Object Samples -Descending
)

$gcPattern =
    'hx::gc::|ScopedPin|ManagedMutator|loadBarrier|storeBarrier|' +
    'concurrentRelocationActive'
$gcRows = @($grouped | Where-Object Symbol -match $gcPattern)
$gcSamples = ($gcRows | Measure-Object Samples -Sum).Sum
$groupedEdges = @(
    $edgeRows |
        Group-Object Leaf, Caller |
        ForEach-Object {
            $samples = (
                $_.Group |
                    Measure-Object Samples -Sum
            ).Sum
            [pscustomobject]@{
                Samples = $samples
                Percent = if ($threadSamples -gt 0) {
                    [math]::Round(
                        $samples * 100.0 / $threadSamples, 3)
                }
                else {
                    0
                }
                Leaf = $_.Group[0].Leaf
                Caller = $_.Group[0].Caller
            }
        } |
        Sort-Object Samples -Descending
)
$gcEdges = @(
    $groupedEdges |
        Where-Object Leaf -match $gcPattern
)

[pscustomobject]@{
    process_id = $ProcessId
    thread_id = $ThreadId
    start_ms = $StartMs
    end_ms = $EndMs
    thread_samples = $threadSamples
    captured_leaf_addresses = $leafRows.Count
    captured_caller_edges = $edgeRows.Count
    resolved_symbol_groups = $grouped.Count
    approximate_gc_samples = $gcSamples
    approximate_gc_percent = if ($threadSamples -gt 0) {
        [math]::Round($gcSamples * 100.0 / $threadSamples, 3)
    }
    else {
        0
    }
    gc_rows = @($gcRows)
    gc_edges = @($gcEdges | Select-Object -First $MaximumRows)
    top_edges = @($groupedEdges | Select-Object -First $MaximumRows)
    top_rows = @($grouped | Select-Object -First $MaximumRows)
} | ConvertTo-Json -Depth 6
