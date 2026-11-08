param(
	[string] $RootDir = (Get-Location).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (!(Test-Path -LiteralPath $RootDir -PathType Container))
{
	throw "Path not found: $RootDir"
}

$crashFiles = Get-ChildItem -LiteralPath $RootDir -Filter "native-crash-*.txt" -File -Recurse
$soFiles = Get-ChildItem -LiteralPath $RootDir -Filter "*.so" -File -Recurse

if ($crashFiles.Count -eq 0)
{
	Write-Host "[warn] No native-crash-*.txt in: $RootDir"
}
if ($soFiles.Count -eq 0)
{
	Write-Host "[warn] No .so files in: $RootDir"
}

function New-ToolPath([string]$basePath, [string]$name)
{
	if ($basePath -and (Test-Path -LiteralPath $basePath))
	{
		$candidate = Join-Path $basePath $name
		if (Test-Path -LiteralPath $candidate)
			return $candidate
	}
	return $null
}

function Resolve-NdkRoot()
{
	$envCandidates = @($env:ANDROID_NDK_HOME, $env:ANDROID_NDK, $env:ANDROID_SDK_ROOT, $env:ANDROID_HOME)
	foreach ($candidate in $envCandidates)
	{
		if (!$candidate) { continue }
		$paths = @(
			(Join-Path $candidate "toolchains/llvm/prebuilt/windows-x86_64/bin"),
			(Join-Path $candidate "prebuilt/windows-x86_64/bin"),
			(Join-Path $candidate "toolchains/llvm/prebuilt/linux-x86_64/bin"),
			(Join-Path $candidate "prebuilt/linux-x86_64/bin")
		)
		foreach ($path in $paths)
		{
			if (Test-Path -LiteralPath $path)
			{
				return (Resolve-Path $path).Path
			}
		}
	}

	$ndkParentCandidates = @()
	if ($env:ANDROID_HOME) { $ndkParentCandidates += Join-Path $env:ANDROID_HOME "ndk" }
	if ($env:LOCALAPPDATA) { $ndkParentCandidates += Join-Path $env:LOCALAPPDATA "Android/Sdk/ndk" }

	foreach ($parent in $ndkParentCandidates)
	{
		if (!(Test-Path -LiteralPath $parent))
			continue
		$items = Get-ChildItem -LiteralPath $parent -Directory | Sort-Object LastWriteTime -Descending
		if ($items.Count -eq 0)
			continue
		foreach ($i in $items)
		{
			$path = Join-Path $i.FullName "toolchains/llvm/prebuilt/windows-x86_64/bin"
			if (Test-Path -LiteralPath $path)
				return (Resolve-Path $path).Path
		}
	}
	return $null
}

function Get-AbiHint([string]$text)
{
	$path = $text.ToLowerInvariant()
	if ($path -match 'arm64') { return "arm64" }
	if ($path -match 'armeabi-v7a|armeabi') { return "armv7" }
	if ($path -match 'x86_64') { return "x86_64" }
	if ($path -match 'x86') { return "x86" }
	return "unknown"
}

function Find-SymbolTool([string]$ndkBin, [string]$soPath)
{
	$abi = Get-AbiHint $soPath
	$byAbi = @{
		"arm64" = @("aarch64-linux-android-addr2line.exe");
		"armv7" = @("armv7a-linux-androideabi-addr2line.exe");
		"x86_64" = @("x86_64-linux-android-addr2line.exe");
		"x86" = @("i686-linux-android-addr2line.exe");
	}
	$names = $byAbi[$abi]
	if (!$names) { $names = @("llvm-symbolizer.exe", "addr2line") }

	foreach ($name in $names)
	{
		$tool = $null
		if ($ndkBin)
			$tool = New-ToolPath $ndkBin $name
		if ($tool -and (Test-Path -LiteralPath $tool))
			return @{ Type = "addr2line"; Path = $tool }
	}

	$symbolizer = Get-Command llvm-symbolizer -ErrorAction SilentlyContinue
	if (!$symbolizer)
		$symbolizer = Get-Command llvm-symbolizer.exe -ErrorAction SilentlyContinue
	if ($symbolizer)
		return @{ Type = "llvm-symbolizer"; Path = $symbolizer.Source }

	foreach ($name in @("addr2line", "addr2line.exe"))
	{
		$t = Get-Command $name -ErrorAction SilentlyContinue
		if ($t) { return @{ Type = "addr2line"; Path = $t.Source } }
	}

	return $null
}

function Parse-Maps([string[]]$lines)
{
	$maps = New-Object System.Collections.Generic.List[PSObject]
	$inMaps = $false

	foreach ($line in $lines)
	{
		if ($line -match '^\[proc_self_maps\]')
		{
			$inMaps = $true
			continue
		}
		if ($inMaps -and $line -match '^\[.+\]$')
		{
			$inMaps = $false
		}

		if ($inMaps -and $line -match '^(?<start>[0-9a-fA-F]{8,16})-(?<end>[0-9a-fA-F]{8,16})\s+\S+\s+(?<off>[0-9a-fA-F]+)\s+\S+\s+\S+\s+(?<path>.+)$')
		{
			$soPath = $matches['path'].Trim()
			if ($soPath -like "*\.so*" -and ($soPath -notlike "*[stack]*") )
			{
				$maps.Add([pscustomobject]@{
					Start = [UInt64]::Parse($matches['start'], [System.Globalization.NumberStyles]::HexNumber)
					End = [UInt64]::Parse($matches['end'], [System.Globalization.NumberStyles]::HexNumber)
					Offset = [UInt64]::Parse($matches['off'], [System.Globalization.NumberStyles]::HexNumber)
					Path = $soPath
					FileName = [IO.Path]::GetFileName($soPath)
					Abi = Get-AbiHint $soPath
				})
			}
		}
	}
	return $maps
}

function Find-MapForAddress([System.Collections.Generic.List[PSObject]]$maps, [UInt64]$address)
{
	foreach ($m in $maps)
	{
		if ($address -ge $m.Start -and $address -lt $m.End)
			return $m
	}
	return $null
}

function Symbolize-Address([string]$soPath, [UInt64]$address, [UInt64]$base, $tool, [bool]$mapMode)
{
	if (!$tool) { return [pscustomobject]@{ Function = "N/A"; Location = "N/A"; Address = "N/A"; Used = "none" } }

	$candidates = @(
		@{Type = "raw"; Value = $address}
	)
	if ($mapMode -and $base -gt 0 -and $address -ge $base)
	{
		$offset = $address - $base
		if ($offset -ne $address)
			$candidates += @{Type = "baseOffset"; Value = $offset}
	}

	foreach ($candidate in $candidates)
	{
		$addrHex = ("0x{0:x}" -f $candidate.Value)
		try
		{
			if ($tool.Type -eq "addr2line")
			{
				$out = & $tool.Path -C -f $addrHex -e $soPath 2>$null
			}
			else
			{
				$out = & $tool.Path --functions --inlining --demangle --obj=$soPath $addrHex 2>$null
			}

			if ($out -and $out.Count -gt 0)
			{
				$txt = [string]($out -join "`n").Trim()
				if ($txt -and ($txt -notmatch "\?\?"))
				{
					if ($tool.Type -eq "addr2line")
					{
						$func = if ($out.Count -gt 0) { "$($out[0])" } else { "??" }
						$loc = if ($out.Count -gt 1) { "$($out[1])" } else { "??:0" }
						return [pscustomobject]@{
							Function = $func
							Location = $loc
							Address = $addrHex
							Used = $candidate.Type
						}
					}
					else
					{
						return [pscustomobject]@{
							Function = $txt
							Location = "see above"
							Address = $addrHex
							Used = $candidate.Type
						}
					}
				}
			}
		}
		catch {}
	}

	return [pscustomobject]@{ Function = "N/A"; Location = "N/A"; Address = ("0x{0:x}" -f $address); Used = "unresolved" }
}

function Get-AddressMatches([string[]]$lines)
{
	$all = New-Object System.Collections.Generic.HashSet[string]
	$inMaps = $false
	foreach ($line in $lines)
	{
		if ($line -match '^\[proc_self_maps\]') { $inMaps = $true; continue }
		if ($inMaps -and $line -match '^\[.+\]$') { $inMaps = $false }
		if ($inMaps) { continue }

		$matches = [regex]::Matches($line, '0x[0-9a-fA-F]{6,16}')
		foreach ($m in $matches)
		{
			$all.Add($m.Value.ToLowerInvariant()) | Out-Null
		}
	}
	return $all
}

$ndkBin = Resolve-NdkRoot
$symbolizerCache = @{}
$soByName = @{}
foreach ($so in $soFiles)
{
	$name = $so.Name
	if (!$soByName.ContainsKey($name))
		$soByName[$name] = New-Object System.Collections.Generic.List[string]
	$soByName[$name].Add($so.FullName)
}

$reportDir = Join-Path $RootDir "symbolicated_reports"
if (!(Test-Path -LiteralPath $reportDir))
	New-Item -Path $reportDir -ItemType Directory | Out-Null

foreach ($crash in $crashFiles)
{
	Write-Host "[info] Process: $($crash.FullName)"
	$lines = Get-Content -LiteralPath $crash.FullName
	$maps = Parse-Maps $lines
	if ($maps.Count -eq 0)
	{
		Write-Host "[warn]  No [proc_self_maps] found in $($crash.Name)"
	}

	$addresses = Get-AddressMatches $lines
	$results = New-Object System.Collections.Generic.List[pscustomobject]

	foreach ($addrText in $addresses)
	{
		$address = [UInt64]::Parse($addrText.Substring(2), [System.Globalization.NumberStyles]::HexNumber)
		$map = Find-MapForAddress $maps $address
		if (-not $map)
			continue

		$soName = $map.FileName
		$cands = @()
		if ($soByName.ContainsKey($soName)) { $cands = $soByName[$soName] }
		if ($cands.Count -eq 0)
			continue

		$localSo = $null
		foreach ($c in $cands)
		{
			if (Get-AbiHint $c -eq $map.Abi)
			{
				$localSo = $c
				break
			}
		}
		if (!$localSo)
			$localSo = $cands[0]

		if (-not $symbolizerCache.ContainsKey($localSo))
			$symbolizerCache[$localSo] = Find-SymbolTool $ndkBin $localSo

		$sym = Symbolize-Address $localSo $address $map.Start $symbolizerCache[$localSo] $true
		$results.Add([pscustomobject]@{
			CrashFile = $crash.Name
			Address = ("0x{0:x}" -f $address)
			Module = $map.Path
			LocalSo = $localSo
			UsedAddress = $sym.Address
			UseOffsetMode = $sym.Used
			Function = $sym.Function
			Location = $sym.Location
		})
	}

	$sorted = $results | Sort-Object Address
	$outTxt = Join-Path $reportDir ("symbolicated_" + $crash.BaseName + ".txt")
	$outCsv = Join-Path $reportDir ("symbolicated_" + $crash.BaseName + ".csv")

	$linesOut = New-Object System.Collections.Generic.List[string]
	$linesOut.Add("CrashFile: $($crash.Name)")
	$linesOut.Add("Maps parsed: $($maps.Count)")
	$linesOut.Add("")
	foreach ($r in $sorted)
	{
		$linesOut.Add("$($r.Address) -> $($r.Function) @ $($r.Location) [so:$($r.LocalSo)] [map:$($r.Module)] [mode:$($r.UseOffsetMode)]")
	}
	Set-Content -LiteralPath $outTxt -Value $linesOut
	$sorted | Export-Csv -Path $outCsv -NoTypeInformation -Encoding UTF8
	Write-Host "[done] $($sorted.Count) frames | $outCsv"
}

Write-Host "[done] Output dir: $reportDir"
