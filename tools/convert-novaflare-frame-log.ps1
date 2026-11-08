[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$input = [IO.Path]::GetFullPath($InputPath)
$output = [IO.Path]::GetFullPath($OutputPath)
$parent = Split-Path -Parent $output
New-Item -ItemType Directory -Path $parent -Force | Out-Null

$number = '[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?'
$pattern = [regex]::new(
    '^frame:time_ms=(?<time>' + $number + ')' +
    ' update_fps=(?<updateFps>' + $number + ')' +
    ' draw_fps=(?<drawFps>' + $number + ')' +
    ' update_low_fps=(?<updateLow>' + $number + ')' +
    ' draw_low_fps=(?<drawLow>' + $number + ')' +
    ' update_ms=(?<updateMs>' + $number + ')' +
    ' draw_ms=(?<drawMs>' + $number + ')' +
    ' update_worst_ms=(?<updateWorst>' + $number + ')' +
    ' draw_worst_ms=(?<drawWorst>' + $number + ')' +
    ' app_mb=(?<app>' + $number + ')' +
    ' gc_mb=(?<gc>' + $number + ')' +
    '(?: wall_time_ms=(?<wallTime>' + $number + '))?$',
    [Text.RegularExpressions.RegexOptions]::CultureInvariant)

$writer = [IO.StreamWriter]::new(
    $output, $false, [Text.UTF8Encoding]::new($false))
$sample = 0
try {
    $writer.WriteLine(
        'sample,time_ms,wall_time_ms,update_fps,draw_fps,' +
        'update_low_fps,draw_low_fps,' +
        'update_ms,draw_ms,update_worst_ms,draw_worst_ms,app_mb,gc_mb')
    if (Test-Path -LiteralPath $input) {
        foreach ($line in [IO.File]::ReadLines($input)) {
            $match = $pattern.Match($line.Trim())
            if (-not $match.Success) {
                continue
            }
            $sample++
            $values = @(
                $sample,
                $match.Groups['time'].Value,
                $match.Groups['wallTime'].Value,
                $match.Groups['updateFps'].Value,
                $match.Groups['drawFps'].Value,
                $match.Groups['updateLow'].Value,
                $match.Groups['drawLow'].Value,
                $match.Groups['updateMs'].Value,
                $match.Groups['drawMs'].Value,
                $match.Groups['updateWorst'].Value,
                $match.Groups['drawWorst'].Value,
                $match.Groups['app'].Value,
                $match.Groups['gc'].Value
            )
            $writer.WriteLine(($values -join ','))
        }
    }
}
finally {
    $writer.Dispose()
}

[pscustomobject]@{
    input = $input
    output = $output
    samples = $sample
}
