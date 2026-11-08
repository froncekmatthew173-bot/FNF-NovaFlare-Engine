[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$SymbolExecutable,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [Parameter(Mandatory = $true)]
    [string]$SymbolTablePath,

    [string]$NmPath = 'C:\msys64\mingw64\bin\nm.exe',

    [UInt64]$ImageBase = 0x140000000
)

$ErrorActionPreference = 'Stop'
$inputFullPath = [IO.Path]::GetFullPath($InputPath)
$symbolFullPath = [IO.Path]::GetFullPath($SymbolExecutable)
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$tableFullPath = [IO.Path]::GetFullPath($SymbolTablePath)

foreach ($requiredPath in @($inputFullPath, $symbolFullPath, $NmPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file is missing: $requiredPath"
    }
}

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $NmPath
$startInfo.Arguments = '-n -C --defined-only "' +
    $symbolFullPath.Replace('"', '\"') + '"'
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true

$process = [Diagnostics.Process]::new()
$process.StartInfo = $startInfo
if (-not $process.Start()) {
    throw 'Failed to start nm.'
}

$addresses = [Collections.Generic.List[UInt64]]::new()
$names = [Collections.Generic.List[string]]::new()
$symbolPattern = [regex]::new(
    '^([0-9a-fA-F]{8,16})\s+([TtWw])\s+(.+)$',
    [Text.RegularExpressions.RegexOptions]::Compiled)

while (($line = $process.StandardOutput.ReadLine()) -ne $null) {
    $match = $symbolPattern.Match($line)
    if (-not $match.Success) {
        continue
    }

    [UInt64]$address = 0
    if (-not [UInt64]::TryParse(
        $match.Groups[1].Value,
        [Globalization.NumberStyles]::HexNumber,
        [Globalization.CultureInfo]::InvariantCulture,
        [ref]$address)) {
        continue
    }
    if ($address -lt $ImageBase) {
        continue
    }

    $name = $match.Groups[3].Value
    if ($name.StartsWith('.text', [StringComparison]::Ordinal)) {
        continue
    }

    if ($addresses.Count -gt 0 -and
        $addresses[$addresses.Count - 1] -eq $address) {
        $names[$names.Count - 1] = $name
    } else {
        $addresses.Add($address)
        $names.Add($name)
    }
}

$stderr = $process.StandardError.ReadToEnd()
$process.WaitForExit()
if ($process.ExitCode -ne 0) {
    throw "nm exited with code $($process.ExitCode): $stderr"
}
if ($addresses.Count -eq 0) {
    throw 'nm produced no usable text symbols.'
}

function Find-Symbol {
    param([UInt64]$RelativeAddress)

    $absoluteAddress = $ImageBase + $RelativeAddress
    $low = 0
    $high = $addresses.Count - 1
    $best = -1
    while ($low -le $high) {
        $middle = $low + [Math]::Floor(($high - $low) / 2)
        $candidate = $addresses[$middle]
        if ($candidate -le $absoluteAddress) {
            $best = $middle
            $low = $middle + 1
        } else {
            $high = $middle - 1
        }
    }
    if ($best -lt 0) {
        return $null
    }

    [pscustomobject]@{
        Name = $names[$best]
        Delta = $absoluteAddress - $addresses[$best]
    }
}

$inputText = [IO.File]::ReadAllText($inputFullPath)
$addressPattern = [regex]::new(
    '(?i)(?<module>(?:[a-z]:\\[^\r\n\t]*)?' +
    'novaflare engine\.exe)\+0x' +
    '(?<address>[0-9a-f]+)',
    [Text.RegularExpressions.RegexOptions]::Compiled)
$frequency = [Collections.Generic.Dictionary[UInt64, int]]::new()
foreach ($match in $addressPattern.Matches($inputText)) {
    [UInt64]$relativeAddress = [Convert]::ToUInt64(
        $match.Groups['address'].Value, 16)
    if ($frequency.ContainsKey($relativeAddress)) {
        $frequency[$relativeAddress]++
    } else {
        $frequency[$relativeAddress] = 1
    }
}

$resolvedText = $addressPattern.Replace($inputText, {
    param($match)
    [UInt64]$relativeAddress = [Convert]::ToUInt64(
        $match.Groups['address'].Value, 16)
    $symbol = Find-Symbol $relativeAddress
    if ($null -eq $symbol) {
        return $match.Value
    }
    return $match.Value + ' [' + $symbol.Name +
        '+0x' + $symbol.Delta.ToString('x') + ']'
})
[IO.File]::WriteAllText(
    $outputFullPath, $resolvedText, [Text.UTF8Encoding]::new($false))

$rows = [Collections.Generic.List[string]]::new()
$rows.Add("occurrences`trelative_address`tsymbol`tdelta")
foreach ($entry in $frequency.GetEnumerator() |
    Sort-Object Value -Descending) {
    $symbol = Find-Symbol $entry.Key
    if ($null -eq $symbol) {
        $rows.Add("$($entry.Value)`t0x$($entry.Key.ToString('x'))`t<unknown>`t")
    } else {
        $rows.Add(
            "$($entry.Value)`t0x$($entry.Key.ToString('x'))`t" +
            "$($symbol.Name)`t0x$($symbol.Delta.ToString('x'))")
    }
}
[IO.File]::WriteAllLines(
    $tableFullPath, $rows, [Text.UTF8Encoding]::new($false))

Write-Output "symbols_loaded=$($addresses.Count)"
Write-Output "addresses_resolved=$($frequency.Count)"
Write-Output "output=$outputFullPath"
Write-Output "symbol_table=$tableFullPath"
