[CmdletBinding()]
param(
    [string]$ReferenceHxcppRoot = 'D:\game\superbackup\git',
    [string]$WindowsHaxe = 'D:\app\haxe\haxe\haxe.exe',
    [string]$SourceHxml = 'export\release\windows\haxe\release.hxml',
    [string]$CurrentGeneratedRoot = 'export\release\windows\obj',
    [string]$ReferenceGeneratedRoot = '_build\hxcpp-original-generator-audit',
    [string]$ReportPath = 'docs\HXCPP_ORIGINAL_PARITY_AUDIT_2026-07-16.md',
    [string]$JsonPath = 'artifacts\novagc\hxcpp-original-parity-audit.json',
    [switch]$SkipReferenceGeneration
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Resolve-WorkspacePath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $workspace $Path))
}

function Get-RelativePath([string]$BasePath, [string]$Path) {
    $baseUri = [Uri](([IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'))
    $pathUri = [Uri][IO.Path]::GetFullPath($Path)
    return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

function Get-TextFiles([string]$Root, [string[]]$Extensions) {
    if (-not (Test-Path -LiteralPath $Root)) {
        return @()
    }
    return @(Get-ChildItem -LiteralPath $Root -File -Recurse | Where-Object {
        $Extensions -contains $_.Extension.ToLowerInvariant()
    })
}

function Get-TokenSet([IO.FileInfo[]]$Files, [string]$Pattern) {
    $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($file in $Files) {
        $text = [IO.File]::ReadAllText($file.FullName)
        foreach ($match in [Text.RegularExpressions.Regex]::Matches($text, $Pattern)) {
            [void]$set.Add($match.Value)
        }
    }
    return ,$set
}

function Get-GeneratedMethodSet([string]$Root) {
    $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    # Stack-frame metadata is emitted from the typed Haxe method set by both
    # generators.  Comparing C++ definitions directly is invalid because old
    # gencpp expands many _dyn functions through macros while NovaGC emits real
    # wrappers and precise-rooted calls.
    $pattern = 'HX_(?:LOCAL|DEFINE)_STACK_FRAME\([^,]+,"(?<owner>[^"]+)","(?<method>[^"]+)"'
    foreach ($file in Get-TextFiles (Join-Path $Root 'src') @('.cpp')) {
        $text = [IO.File]::ReadAllText($file.FullName)
        foreach ($match in [Text.RegularExpressions.Regex]::Matches($text, $pattern)) {
            $owner = $match.Groups['owner'].Value
            $method = $match.Groups['method'].Value
            [void]$set.Add("$owner::$method")
        }
    }
    return ,$set
}

function Get-GeneratedFieldSet([string]$Root) {
    $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $pattern = '(?m)^\s*(?:static\s+)?[^\r\n;(){}]+\s+(?<name>[A-Za-z_]\w*)\s*;\s*$'
    foreach ($file in Get-TextFiles (Join-Path $Root 'include') @('.h', '.hpp')) {
        $relative = Get-RelativePath (Join-Path $Root 'include') $file.FullName
        $text = [IO.File]::ReadAllText($file.FullName)
        foreach ($match in [Text.RegularExpressions.Regex]::Matches($text, $pattern)) {
            $name = $match.Groups['name'].Value
            # Old gencpp emits GC/vtable macro members that are intentionally
            # replaced by precise descriptors.  They are not Haxe data fields.
            if ($name -notin @('super', 'OBJ_') -and $name -notmatch '^(_hx_|HX_|__)') {
                [void]$set.Add("$relative::$name")
            }
        }
    }
    return ,$set
}

function Get-BuildXmlEntrySet([string]$Root) {
    $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $path = Join-Path $Root 'Build.xml'
    if (-not (Test-Path -LiteralPath $path)) {
        return ,$set
    }
    $text = [IO.File]::ReadAllText($path)
    $pattern = '<(?<kind>file|include|lib|compilerflag|linkerflag)\s+[^>]*?(?:name|value)="(?<value>[^"]+)"'
    foreach ($match in [Text.RegularExpressions.Regex]::Matches($text, $pattern)) {
        [void]$set.Add(($match.Groups['kind'].Value + ':' + $match.Groups['value'].Value).Replace('/', '\'))
    }
    return ,$set
}

function Get-FileSet([string]$Root, [string]$Child, [string[]]$Extensions) {
    $path = Join-Path $Root $Child
    $set = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in Get-TextFiles $path $Extensions) {
        [void]$set.Add((Get-RelativePath $path $file.FullName))
    }
    return ,$set
}

function Get-SetDifference($Left, $Right) {
    return @($Left | Where-Object { -not $Right.Contains($_) } | Sort-Object)
}

function Get-OptionMap([string]$Root) {
    $map = [ordered]@{}
    $path = Join-Path $Root 'Options.txt'
    if (Test-Path -LiteralPath $path) {
        foreach ($line in [IO.File]::ReadAllLines($path)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $parts = $line.Split('=', 2)
            $map[$parts[0]] = if ($parts.Count -eq 2) { $parts[1] } else { '1' }
        }
    }
    return $map
}

$referenceRoot = Resolve-WorkspacePath $ReferenceHxcppRoot
$haxePath = Resolve-WorkspacePath $WindowsHaxe
$hxmlPath = Resolve-WorkspacePath $SourceHxml
$currentRoot = Resolve-WorkspacePath $CurrentGeneratedRoot
$generatedReferenceRoot = Resolve-WorkspacePath $ReferenceGeneratedRoot
$reportFile = Resolve-WorkspacePath $ReportPath
$jsonFile = Resolve-WorkspacePath $JsonPath

if (-not $SkipReferenceGeneration) {
    if (-not (Test-Path -LiteralPath $haxePath)) {
        throw "Reference Windows Haxe not found: $haxePath"
    }
    if (Test-Path -LiteralPath $generatedReferenceRoot) {
        $resolvedBuildRoot = [IO.Path]::GetFullPath((Join-Path $workspace '_build')).TrimEnd('\') + '\'
        $resolvedTarget = [IO.Path]::GetFullPath($generatedReferenceRoot).TrimEnd('\') + '\'
        if (-not $resolvedTarget.StartsWith($resolvedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to replace reference output outside _build: $generatedReferenceRoot"
        }
        Remove-Item -LiteralPath $generatedReferenceRoot -Recurse -Force
    }

    $auditBuildRoot = Join-Path $workspace '_build\hxcpp-parity-audit'
    New-Item -ItemType Directory -Path $auditBuildRoot -Force | Out-Null
    $temporaryHxml = Join-Path $auditBuildRoot 'reference-windows.hxml'
    $hxml = [IO.File]::ReadAllText($hxmlPath)
    $hxml = [Text.RegularExpressions.Regex]::Replace($hxml, '(?m)^-D\s+(?:hxcpp_zgc|novagc-precise)\s*\r?\n?', '')
    $hxml = [Text.RegularExpressions.Regex]::Replace($hxml, '(?m)^-cp\s+[^\r\n]*[\\/]hxcpp[\\/]src\s*\r?\n?', '')
    $referenceOutput = (Get-RelativePath $workspace $generatedReferenceRoot).Replace('\', '/')
    $hxml = [Text.RegularExpressions.Regex]::Replace($hxml, '(?m)^-cpp\s+[^\r\n]+\r?$', "-cpp $referenceOutput")
    [IO.File]::WriteAllText($temporaryHxml, $hxml, [Text.UTF8Encoding]::new($false))

    $oldStd = $env:HAXE_STD_PATH
    try {
        $env:HAXE_STD_PATH = Join-Path $workspace 'toolchains\haxe-novagc\std'
        # Windows PowerShell promotes any native stderr line to a terminating
        # NativeCommandError while ErrorActionPreference is Stop.  Haxe emits
        # legitimate deprecation warnings on stderr, so capture both streams
        # explicitly and decide success only from the process exit code.
        $stdoutPath = Join-Path $auditBuildRoot 'reference-generation.stdout.log'
        $stderrPath = Join-Path $auditBuildRoot 'reference-generation.stderr.log'
        $process = Start-Process -FilePath $haxePath `
            -ArgumentList $temporaryHxml -WindowStyle Hidden -Wait -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
        $output = @(
            Get-Content -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
            Get-Content -LiteralPath $stderrPath -ErrorAction SilentlyContinue
        )
        $exitCode = $process.ExitCode
        $output | Set-Content -LiteralPath (Join-Path $auditBuildRoot 'reference-generation.log') -Encoding UTF8
        if ($exitCode -ne 0) {
            throw "Reference generator failed with exit code $exitCode. See $auditBuildRoot\reference-generation.log"
        }
    }
    finally {
        $env:HAXE_STD_PATH = $oldStd
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $generatedReferenceRoot 'Build.xml'))) {
    throw "Reference generator output is incomplete: $generatedReferenceRoot"
}

$referenceMethods = Get-GeneratedMethodSet $generatedReferenceRoot
$currentMethods = Get-GeneratedMethodSet $currentRoot
$referenceFields = Get-GeneratedFieldSet $generatedReferenceRoot
$currentFields = Get-GeneratedFieldSet $currentRoot
$referenceSources = Get-FileSet $generatedReferenceRoot 'src' @('.cpp', '.c', '.mm')
$currentSources = Get-FileSet $currentRoot 'src' @('.cpp', '.c', '.mm')
$referenceHeaders = Get-FileSet $generatedReferenceRoot 'include' @('.h', '.hpp')
$currentHeaders = Get-FileSet $currentRoot 'include' @('.h', '.hpp')
$referenceBuildEntries = Get-BuildXmlEntrySet $generatedReferenceRoot
$currentBuildEntries = Get-BuildXmlEntrySet $currentRoot
$referenceOptions = Get-OptionMap $generatedReferenceRoot
$currentOptions = Get-OptionMap $currentRoot

$backupHeaders = Get-TextFiles (Join-Path $referenceRoot 'include') @('.h', '.hpp')
$currentRuntimeFiles = @(
    Get-TextFiles (Join-Path $workspace 'hxcpp') @('.h', '.hpp', '.cpp', '.c', '.hx')
) + @(
    Get-TextFiles (Join-Path $workspace 'toolchains\haxe-novagc\src\generators') @('.ml', '.mli')
)
$publicPatterns = [ordered]@{
    hxcpp = '__hxcpp_[A-Za-z0-9_]+'
    cffi = '(?:alloc|val|buffer|kind|api|hx)_[A-Za-z][A-Za-z0-9_]+'
    macro = 'HX_[A-Z][A-Z0-9_]+'
}
$publicAudit = [ordered]@{}
$currentPublicSets = [ordered]@{}
foreach ($category in $publicPatterns.Keys) {
    $backupSet = Get-TokenSet $backupHeaders $publicPatterns[$category]
    $currentSet = Get-TokenSet $currentRuntimeFiles $publicPatterns[$category]
    $currentPublicSets[$category] = $currentSet
    $publicAudit[$category] = [ordered]@{
        referenceCount = $backupSet.Count
        currentExactCount = @($backupSet | Where-Object { $currentSet.Contains($_) }).Count
        missing = @(Get-SetDifference $backupSet $currentSet)
    }
}

$headerAudit = @()
$currentAll = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($category in $publicPatterns.Keys) {
    foreach ($value in $currentPublicSets[$category]) {
        [void]$currentAll.Add($value)
    }
}
foreach ($header in $backupHeaders | Sort-Object FullName) {
    $symbols = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $text = [IO.File]::ReadAllText($header.FullName)
    foreach ($pattern in $publicPatterns.Values) {
        foreach ($match in [Text.RegularExpressions.Regex]::Matches($text, $pattern)) {
            [void]$symbols.Add($match.Value)
        }
    }
    $missing = @(Get-SetDifference $symbols $currentAll)
    $headerAudit += [ordered]@{
        header = Get-RelativePath (Join-Path $referenceRoot 'include') $header.FullName
        symbols = $symbols.Count
        exact = $symbols.Count - $missing.Count
        missing = $missing
    }
}

$activeClassPaths = @()
foreach ($line in [IO.File]::ReadAllLines($hxmlPath)) {
    if ($line -match '^-cp\s+(.+)$') {
        $path = $Matches[1].Trim()
        if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $workspace $path }
        if (Test-Path -LiteralPath $path) { $activeClassPaths += [IO.Path]::GetFullPath($path) }
    }
}
$hostTargetSites = @()
foreach ($path in $activeClassPaths | Sort-Object -Unique) {
    foreach ($file in Get-TextFiles $path @('.hx')) {
        $lines = [IO.File]::ReadAllLines($file.FullName)
        for ($index = 0; $index -lt $lines.Length; $index++) {
            if ($lines[$index] -match 'Sys\.systemName\(\)') {
                $whole = [IO.File]::ReadAllText($file.FullName)
                $hostTargetSites += [ordered]@{
                    file = Get-RelativePath $workspace $file.FullName
                    line = $index + 1
                    macroContext = $whole -match 'haxe\.macro|#if\s+macro'
                    text = $lines[$index].Trim()
                }
            }
        }
    }
}

$optionDifferences = @()
foreach ($name in @($referenceOptions.Keys + $currentOptions.Keys) | Sort-Object -Unique) {
    $referenceValue = if ($referenceOptions.Contains($name)) { $referenceOptions[$name] } else { $null }
    $currentValue = if ($currentOptions.Contains($name)) { $currentOptions[$name] } else { $null }
    if ($referenceValue -ne $currentValue -and $name -notin @('hxcpp_zgc', 'novagc_precise', 'hxcpp')) {
        $optionDifferences += [ordered]@{ name = $name; reference = $referenceValue; current = $currentValue }
    }
}

# Some old-gencpp implementation details are intentionally replaced rather
# than reproduced in the precise runtime.  Keep the raw differences visible,
# but separate them from genuinely unaccounted target-surface gaps.  Every
# entry here must name the replacement and its regression evidence.
$knownFieldReplacements = [ordered]@{
    'cpp\Int64Map.h::h' = 'Private legacy hash handle replaced by exact-scannable novaKeys/novaValues/novaStates storage; official Haxe Map tests pass.'
    'haxe\ds\IntMap.h::h' = 'Private legacy hash handle replaced by exact-scannable novaKeys/novaValues/novaStates storage; official Haxe Map tests pass.'
    'haxe\ds\ObjectMap.h::h' = 'Private legacy hash handle replaced by exact-scannable novaKeys/novaValues/novaStates storage; official Haxe Map tests pass.'
    'haxe\ds\StringMap.h::h' = 'Private legacy hash handle replaced by exact-scannable novaKeys/novaValues/novaStates storage; official Haxe Map tests pass.'
}
$knownBuildEntryReplacements = [ordered]@{
    'file:${HXCPP}\src\hx\NoFiles.cpp' = 'NovaGC omits the debugger file table and its boot call together when HXCPP_DEBUGGER is disabled; stack-trace regressions cover the active path.'
    'file:src\haxe\NativeStackTrace.cpp' = 'haxe.NativeStackTrace is implemented by hxcpp/runtime/core/stack_trace.cpp and the NativeStackTrace_obj facade; official CallStack tests pass.'
    'file:src\sys\thread\_Thread\HaxeThread.cpp' = 'The cpp std Thread backend is replaced by exact-rooted ZgcThread plus hxcpp/runtime/core/thread.cpp; thread/event-loop probes and kernel thread tests pass.'
    'include:${HXCPP}\build-tool\BuildCommon.xml' = 'The legacy XML build driver is replaced by hxcpp/tools/HxcppZgcBuild.hx, which compiled the complete Windows source graph.'
}

$rawMissingFields = @(Get-SetDifference $referenceFields $currentFields)
$rawMissingBuildEntries = @(Get-SetDifference $referenceBuildEntries $currentBuildEntries)
$fieldReplacements = @($rawMissingFields | Where-Object {
    $knownFieldReplacements.Contains($_)
} | ForEach-Object {
    [ordered]@{ name = $_; replacement = $knownFieldReplacements[$_] }
})
$buildEntryReplacements = @($rawMissingBuildEntries | Where-Object {
    $knownBuildEntryReplacements.Contains($_)
} | ForEach-Object {
    [ordered]@{ name = $_; replacement = $knownBuildEntryReplacements[$_] }
})
$unaccountedMissingFields = @($rawMissingFields | Where-Object {
    -not $knownFieldReplacements.Contains($_)
})
$unaccountedMissingBuildEntries = @($rawMissingBuildEntries | Where-Object {
    -not $knownBuildEntryReplacements.Contains($_)
})

$result = [ordered]@{
    generatedAt = (Get-Date).ToString('o')
    referenceHxcpp = $referenceRoot
    referenceGenerator = $haxePath
    referenceGeneratedRoot = $generatedReferenceRoot
    currentGeneratedRoot = $currentRoot
    generator = [ordered]@{
        referenceSources = $referenceSources.Count
        currentSources = $currentSources.Count
        sourcesMissingFromCurrent = @(Get-SetDifference $referenceSources $currentSources)
        sourcesOnlyInCurrent = @(Get-SetDifference $currentSources $referenceSources)
        referenceHeaders = $referenceHeaders.Count
        currentHeaders = $currentHeaders.Count
        headersMissingFromCurrent = @(Get-SetDifference $referenceHeaders $currentHeaders)
        methodsMissingFromCurrent = @(Get-SetDifference $referenceMethods $currentMethods)
        methodsOnlyInCurrent = @(Get-SetDifference $currentMethods $referenceMethods)
        fieldsMissingFromCurrent = $rawMissingFields
        fieldReplacements = $fieldReplacements
        unaccountedFieldsMissingFromCurrent = $unaccountedMissingFields
        buildEntriesMissingFromCurrent = $rawMissingBuildEntries
        buildEntryReplacements = $buildEntryReplacements
        unaccountedBuildEntriesMissingFromCurrent = $unaccountedMissingBuildEntries
        buildEntriesOnlyInCurrent = @(Get-SetDifference $currentBuildEntries $referenceBuildEntries)
        optionDifferences = $optionDifferences
        hostTargetSites = $hostTargetSites
    }
    publicAbi = $publicAudit
    headers = $headerAudit
}

New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($jsonFile)) -Force | Out-Null
[IO.File]::WriteAllText($jsonFile, ($result | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))

function Add-List([Text.StringBuilder]$Builder, [string]$Title, [object[]]$Items, [int]$Limit = 0) {
    [void]$Builder.AppendLine("## $Title")
    [void]$Builder.AppendLine()
    if ($Items.Count -eq 0) {
        [void]$Builder.AppendLine('- None.')
    }
    else {
        $shown = if ($Limit -gt 0) { @($Items | Select-Object -First $Limit) } else { $Items }
        foreach ($item in $shown) { [void]$Builder.AppendLine("- ``$item``") }
        if ($shown.Count -lt $Items.Count) { [void]$Builder.AppendLine("- ...and $($Items.Count - $shown.Count) more entries in the JSON report.") }
    }
    [void]$Builder.AppendLine()
}

$md = [Text.StringBuilder]::new()
[void]$md.AppendLine('# Original hxcpp/gencpp versus NovaGC exhaustive machine audit')
[void]$md.AppendLine()
[void]$md.AppendLine("> Generated: $($result.generatedAt)  ")
[void]$md.AppendLine("> Original hxcpp teacher: ``$referenceRoot``  ")
[void]$md.AppendLine("> Original generator: ``$haxePath``  ")
[void]$md.AppendLine('> Exact names and generated-output parity are only first-layer evidence. Tests must still prove behavior, exceptions, threading, and GC lifetime semantics.')
[void]$md.AppendLine()
[void]$md.AppendLine('## Summary')
[void]$md.AppendLine()
[void]$md.AppendLine("- Reference generated sources: $($referenceSources.Count); NovaGC: $($currentSources.Count); missing: $($result.generator.sourcesMissingFromCurrent.Count).")
[void]$md.AppendLine("- Reference-generated methods missing from NovaGC: $($result.generator.methodsMissingFromCurrent.Count).")
[void]$md.AppendLine("- Reference-generated field names structurally different in NovaGC: $($result.generator.fieldsMissingFromCurrent.Count); classified replacements $($fieldReplacements.Count); unaccounted $($unaccountedMissingFields.Count).")
[void]$md.AppendLine("- Reference Build.xml/native entries structurally different in NovaGC: $($result.generator.buildEntriesMissingFromCurrent.Count); classified replacements $($buildEntryReplacements.Count); unaccounted $($unaccountedMissingBuildEntries.Count).")
[void]$md.AppendLine("- Backup public ``__hxcpp_*`` names: $($publicAudit.hxcpp.referenceCount); exact-name coverage $($publicAudit.hxcpp.currentExactCount); missing $($publicAudit.hxcpp.missing.Count).")
[void]$md.AppendLine("- Backup public CFFI-style names: $($publicAudit.cffi.referenceCount); exact-name coverage $($publicAudit.cffi.currentExactCount); missing $($publicAudit.cffi.missing.Count).")
[void]$md.AppendLine("- Backup public ``HX_*`` macros: $($publicAudit.macro.referenceCount); exact-name coverage $($publicAudit.macro.currentExactCount); missing $($publicAudit.macro.missing.Count).")
[void]$md.AppendLine("- Active Haxe classpath sites depending on compiler-host ``Sys.systemName()``: $($hostTargetSites.Count); macro-context sites $(@($hostTargetSites | Where-Object macroContext).Count).")
[void]$md.AppendLine()

Add-List $md 'Reference-generated methods missing from NovaGC' $result.generator.methodsMissingFromCurrent
Add-List $md 'Reference-generated source files missing from NovaGC' $result.generator.sourcesMissingFromCurrent
Add-List $md 'Reference Build.xml/native entries missing from NovaGC' $result.generator.buildEntriesMissingFromCurrent
Add-List $md 'Reference-generated field names missing from NovaGC' $result.generator.fieldsMissingFromCurrent 300

[void]$md.AppendLine('## Classified precise-runtime replacements')
[void]$md.AppendLine()
foreach ($entry in @($fieldReplacements + $buildEntryReplacements)) {
    [void]$md.AppendLine("- ``$($entry.name)`` -> $($entry.replacement)")
}
[void]$md.AppendLine()
Add-List $md 'Unaccounted generated field differences' $unaccountedMissingFields
Add-List $md 'Unaccounted Build.xml/native entry differences' $unaccountedMissingBuildEntries
Add-List $md 'Missing backup public __hxcpp_* names' $publicAudit.hxcpp.missing
Add-List $md 'Missing backup public CFFI-style names' $publicAudit.cffi.missing
Add-List $md 'Missing backup public HX_* macros' $publicAudit.macro.missing

[void]$md.AppendLine('## Exact-name coverage of the 56 original public headers')
[void]$md.AppendLine()
[void]$md.AppendLine('| Original header | Detected public names | Exact-name coverage | Missing |')
[void]$md.AppendLine('|---|---:|---:|---:|')
foreach ($entry in $headerAudit) {
    [void]$md.AppendLine("| ``$($entry.header)`` | $($entry.symbols) | $($entry.exact) | $($entry.missing.Count) |")
}
[void]$md.AppendLine()

[void]$md.AppendLine('## Windows target versus Linux/Docker compiler-host branch risks')
[void]$md.AppendLine()
foreach ($site in $hostTargetSites) {
    $kind = if ($site.macroContext) { 'macro/compile-time risk' } else { 'runtime code' }
    [void]$md.AppendLine("- [$kind] ``$($site.file):$($site.line)`` - ``$($site.text.Replace('`', '\`'))``")
}
[void]$md.AppendLine()
[void]$md.AppendLine('The first proven real divergence is ``trandom.InitMacro.checkWindows()`` checking the Docker/Linux compiler host. The Windows target consequently lacks ``trandom_windows`` and omits ``Native.getWindows``, ``CPPExtern``, and ``trandom_native.c``. Host/target isolation and a generator-differential gate are required.')
[void]$md.AppendLine()
[void]$md.AppendLine('## Acceptance discipline')
[void]$md.AppendLine()
[void]$md.AppendLine('1. Every reference-generated method, field, and native entry missing from NovaGC must reach zero or have tested target-inapplicability evidence.')
[void]$md.AppendLine('2. Public ABI gaps cannot be satisfied by same-name stubs; each group needs signature, result, exception, thread, and GC-lifetime tests.')
[void]$md.AppendLine('3. Regenerate and run the complete game route only after the generator-differential gate passes, instead of discovering one omission per run.')
[void]$md.AppendLine('4. Complete raw data is stored in ``artifacts/novagc/hxcpp-original-parity-audit.json``.')

New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($reportFile)) -Force | Out-Null
[IO.File]::WriteAllText($reportFile, $md.ToString(), [Text.UTF8Encoding]::new($false))

[pscustomobject]@{
    Report = $reportFile
    Json = $jsonFile
    MissingMethods = $result.generator.methodsMissingFromCurrent.Count
    MissingFields = $result.generator.fieldsMissingFromCurrent.Count
    MissingBuildEntries = $result.generator.buildEntriesMissingFromCurrent.Count
    UnaccountedFields = $unaccountedMissingFields.Count
    UnaccountedBuildEntries = $unaccountedMissingBuildEntries.Count
    MissingHxcppSymbols = $publicAudit.hxcpp.missing.Count
    MissingCffiSymbols = $publicAudit.cffi.missing.Count
    MissingMacros = $publicAudit.macro.missing.Count
} | Format-List
