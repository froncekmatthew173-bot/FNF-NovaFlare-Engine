[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BaselineDirectory,

    [Parameter(Mandatory = $true)]
    [string]$CandidateDirectory
)

$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Convert-Number([object]$Value) {
    return [double]::Parse([string]$Value, $invariant)
}

function Get-Percentile([double[]]$Values, [double]$Percentile) {
    if ($Values.Count -eq 0) {
        return $null
    }
    $sorted = @($Values | Sort-Object)
    $position = ($sorted.Count - 1) * $Percentile
    $low = [math]::Floor($position)
    $high = [math]::Ceiling($position)
    if ($low -eq $high) {
        return [double]$sorted[$low]
    }
    return [double]$sorted[$low] +
        ([double]$sorted[$high] - [double]$sorted[$low]) *
        ($position - $low)
}

function Round-Number([object]$Value, [int]$Digits = 3) {
    if ($null -eq $Value) {
        return $null
    }
    return [math]::Round([double]$Value, $Digits)
}

function Get-Average([double[]]$Values) {
    if ($Values.Count -eq 0) {
        return $null
    }
    return ($Values | Measure-Object -Average).Average
}

function Get-Maximum([double[]]$Values) {
    if ($Values.Count -eq 0) {
        return $null
    }
    return ($Values | Measure-Object -Maximum).Maximum
}

function Get-RunAnalysis([string]$Name, [string]$Directory) {
    $frames = @(Import-Csv -LiteralPath (
            Join-Path $Directory 'frame-rate-from-haxe.csv'))
    $gcCycles = @(Import-Csv -LiteralPath (
            Join-Path $Directory 'gc-cycles.csv'))
    $processSamples = @(Import-Csv -LiteralPath (
            Join-Path $Directory 'process-samples.csv'))
    $hardwarePath = Join-Path $Directory 'hardware-samples.csv'
    $hardwareSamples = if (Test-Path -LiteralPath $hardwarePath) {
        @(Import-Csv -LiteralPath $hardwarePath)
    }
    else {
        @()
    }

    $baseCandidates = foreach ($frame in $frames) {
        (Convert-Number $frame.wall_time_ms) -
            (Convert-Number $frame.time_ms)
    }
    $appEpochMs = Get-Percentile ([double[]]$baseCandidates) 0.5

    foreach ($cycle in $gcCycles) {
        $cycle | Add-Member -NotePropertyName app_ms -NotePropertyValue (
            (Convert-Number $cycle.timestamp_us) / 1000 - $appEpochMs)
    }
    foreach ($sample in $processSamples) {
        $epochMs = ([DateTimeOffset]::Parse(
                $sample.utc,
                $invariant,
                [Globalization.DateTimeStyles]::AssumeUniversal
            )).ToUnixTimeMilliseconds()
        $sample | Add-Member -NotePropertyName app_ms -NotePropertyValue (
            $epochMs - $appEpochMs)
    }
    foreach ($sample in $hardwareSamples) {
        $epochMs = ([DateTimeOffset]::Parse(
                $sample.utc,
                $invariant,
                [Globalization.DateTimeStyles]::AssumeUniversal
            )).ToUnixTimeMilliseconds()
        $sample | Add-Member -NotePropertyName app_ms -NotePropertyValue (
            $epochMs - $appEpochMs)
    }

    function Get-Window([double]$FromSeconds, [double]$ToSeconds) {
        $windowFrames = @($frames | Where-Object {
                (Convert-Number $_.time_ms) -ge $FromSeconds * 1000 -and
                (Convert-Number $_.time_ms) -lt $ToSeconds * 1000
            })
        $windowGc = @($gcCycles | Where-Object {
                $_.app_ms -ge $FromSeconds * 1000 -and
                $_.app_ms -lt $ToSeconds * 1000
            })
        $windowProcess = @($processSamples | Where-Object {
                $_.app_ms -ge $FromSeconds * 1000 -and
                $_.app_ms -lt $ToSeconds * 1000
            })
        $windowHardware = @($hardwareSamples | Where-Object {
                $_.app_ms -ge $FromSeconds * 1000 -and
                $_.app_ms -lt $ToSeconds * 1000
            })

        $updateFps = [double[]]@(
            $windowFrames | ForEach-Object {
                Convert-Number $_.update_fps
            })
        $drawFps = [double[]]@(
            $windowFrames | ForEach-Object {
                Convert-Number $_.draw_fps
            })
        $updateLowFps = [double[]]@(
            $windowFrames | ForEach-Object {
                Convert-Number $_.update_low_fps
            })
        $drawLowFps = [double[]]@(
            $windowFrames | ForEach-Object {
                Convert-Number $_.draw_low_fps
            })
        $updateWorstMs = [double[]]@(
            $windowFrames | ForEach-Object {
                Convert-Number $_.update_worst_ms
            })
        $drawWorstMs = [double[]]@(
            $windowFrames | ForEach-Object {
                Convert-Number $_.draw_worst_ms
            })
        $pausesMs = [double[]]@(
            $windowGc | ForEach-Object {
                (Convert-Number $_.pause_us) / 1000
            })
        $concurrentMs = [double[]]@(
            $windowGc | ForEach-Object {
                (Convert-Number $_.concurrent_us) / 1000
            })
        $cpuPercent = [double[]]@(
            $windowProcess | ForEach-Object {
                Convert-Number $_.cpu_percent
            })
        $privateMb = [double[]]@(
            $windowProcess | ForEach-Object {
                Convert-Number $_.private_mb
            })
        $workingSetMb = [double[]]@(
            $windowProcess | ForEach-Object {
                Convert-Number $_.working_set_mb
            })
        $cpuPerformance = [double[]]@(
            $windowHardware | ForEach-Object {
                Convert-Number $_.cpu_performance_percent
            })
        $cpuUtility = [double[]]@(
            $windowHardware | ForEach-Object {
                Convert-Number $_.cpu_utility_percent
            })
        $gpuTotal = [double[]]@(
            $windowHardware | ForEach-Object {
                Convert-Number $_.gpu_total_percent
            })
        $gpu3d = [double[]]@(
            $windowHardware | ForEach-Object {
                Convert-Number $_.gpu_3d_percent
            })
        $gpuDedicatedMb = [double[]]@(
            $windowHardware | ForEach-Object {
                Convert-Number $_.gpu_dedicated_mb
            })
        $appMb = [double[]]@(
            $windowFrames | ForEach-Object {
                Convert-Number $_.app_mb
            })
        $gcMb = [double[]]@(
            $windowFrames | ForEach-Object {
                Convert-Number $_.gc_mb
            })

        $allocationCounters = [double[]]@(
            $windowGc |
                Sort-Object { Convert-Number $_.timestamp_us } |
                ForEach-Object {
                    Convert-Number $_.allocated_bytes
                })
        $allocatedBytes = if ($allocationCounters.Count -ge 2) {
            $allocationCounters[-1] - $allocationCounters[0]
        }
        else {
            0
        }

        return [ordered]@{
            range = "$FromSeconds-$ToSeconds"
            frames = $windowFrames.Count
            gc = $windowGc.Count
            young = @($windowGc | Where-Object kind -eq 'young').Count
            full = @($windowGc | Where-Object kind -eq 'full').Count
            process = $windowProcess.Count
            update = [ordered]@{
                mean = Round-Number (Get-Average $updateFps)
                p5 = Round-Number (
                    Get-Percentile $updateFps 0.05)
                low_mean = Round-Number (Get-Average $updateLowFps)
                worst_p95 = Round-Number (
                    Get-Percentile $updateWorstMs 0.95)
                worst_p99 = Round-Number (
                    Get-Percentile $updateWorstMs 0.99)
                worst_max = Round-Number (
                    Get-Maximum $updateWorstMs)
                ge20 = @($updateWorstMs | Where-Object { $_ -ge 20 }).Count
                ge33 = @($updateWorstMs | Where-Object { $_ -ge 33 }).Count
                ge50 = @($updateWorstMs | Where-Object { $_ -ge 50 }).Count
                ge100 = @($updateWorstMs | Where-Object { $_ -ge 100 }).Count
            }
            draw = [ordered]@{
                mean = Round-Number (Get-Average $drawFps)
                p5 = Round-Number (
                    Get-Percentile $drawFps 0.05)
                low_mean = Round-Number (Get-Average $drawLowFps)
                worst_p95 = Round-Number (
                    Get-Percentile $drawWorstMs 0.95)
                worst_p99 = Round-Number (
                    Get-Percentile $drawWorstMs 0.99)
                worst_max = Round-Number (
                    Get-Maximum $drawWorstMs)
                ge20 = @($drawWorstMs | Where-Object { $_ -ge 20 }).Count
                ge33 = @($drawWorstMs | Where-Object { $_ -ge 33 }).Count
                ge50 = @($drawWorstMs | Where-Object { $_ -ge 50 }).Count
                ge100 = @($drawWorstMs | Where-Object { $_ -ge 100 }).Count
            }
            gc_stats = [ordered]@{
                pause_mean_ms = Round-Number (
                    Get-Average $pausesMs)
                pause_p50_ms = Round-Number (
                    Get-Percentile $pausesMs 0.5)
                pause_p95_ms = Round-Number (
                    Get-Percentile $pausesMs 0.95)
                pause_p99_ms = Round-Number (
                    Get-Percentile $pausesMs 0.99)
                pause_max_ms = Round-Number (
                    Get-Maximum $pausesMs)
                pause_sum_ms = Round-Number (
                    ($pausesMs | Measure-Object -Sum).Sum)
                concurrent_mean_ms = Round-Number (
                    Get-Average $concurrentMs)
                concurrent_max_ms = Round-Number (
                    Get-Maximum $concurrentMs)
                alloc_mib_s = Round-Number (
                    $allocatedBytes / 1MB /
                    ($ToSeconds - $FromSeconds))
            }
            process_stats = [ordered]@{
                cpu_mean = Round-Number (
                    Get-Average $cpuPercent)
                cpu_p95 = Round-Number (
                    Get-Percentile $cpuPercent 0.95)
                cpu_max = Round-Number (
                    Get-Maximum $cpuPercent)
                private_mean_mb = Round-Number (
                    Get-Average $privateMb)
                private_max_mb = Round-Number (
                    Get-Maximum $privateMb)
                ws_mean_mb = Round-Number (
                    Get-Average $workingSetMb)
                ws_max_mb = Round-Number (
                    Get-Maximum $workingSetMb)
                responding_zero = @(
                    $windowProcess |
                        Where-Object {
                            (Convert-Number $_.responding) -eq 0
                        }).Count
                foreground_zero = @(
                    $windowProcess |
                        Where-Object {
                            $null -ne $_.foreground -and
                            (Convert-Number $_.foreground) -eq 0
                        }).Count
                visible_zero = @(
                    $windowProcess |
                        Where-Object {
                            $null -ne $_.visible -and
                            (Convert-Number $_.visible) -eq 0
                        }).Count
                iconic_one = @(
                    $windowProcess |
                        Where-Object {
                            $null -ne $_.iconic -and
                            (Convert-Number $_.iconic) -eq 1
                        }).Count
                cloaked_one = @(
                    $windowProcess |
                        Where-Object {
                            $null -ne $_.cloaked -and
                            (Convert-Number $_.cloaked) -eq 1
                        }).Count
            }
            hardware_stats = [ordered]@{
                samples = $windowHardware.Count
                cpu_performance_mean = Round-Number (
                    Get-Average $cpuPerformance)
                cpu_performance_p95 = Round-Number (
                    Get-Percentile $cpuPerformance 0.95)
                cpu_utility_mean = Round-Number (
                    Get-Average $cpuUtility)
                gpu_total_mean = Round-Number (
                    Get-Average $gpuTotal)
                gpu_total_p95 = Round-Number (
                    Get-Percentile $gpuTotal 0.95)
                gpu_3d_mean = Round-Number (
                    Get-Average $gpu3d)
                gpu_3d_p95 = Round-Number (
                    Get-Percentile $gpu3d 0.95)
                gpu_dedicated_mean_mb = Round-Number (
                    Get-Average $gpuDedicatedMb)
            }
            heap = [ordered]@{
                app_mean_mb = Round-Number (
                    Get-Average $appMb)
                app_max_mb = Round-Number (
                    Get-Maximum $appMb)
                gc_mean_mb = Round-Number (
                    Get-Average $gcMb)
                gc_max_mb = Round-Number (
                    Get-Maximum $gcMb)
            }
        }
    }

    $fullCycles = foreach (
        $cycle in @($gcCycles | Where-Object kind -eq 'full')
    ) {
        [ordered]@{
            cycle = [int]$cycle.cycle
            app_s = Round-Number ($cycle.app_ms / 1000)
            initial_ms = Round-Number (
                (Convert-Number $cycle.initial_pause_us) / 1000)
            concurrent_ms = Round-Number (
                (Convert-Number $cycle.concurrent_us) / 1000)
            final_ms = Round-Number (
                (Convert-Number $cycle.final_pause_us) / 1000)
            pause_ms = Round-Number (
                (Convert-Number $cycle.pause_us) / 1000)
            used_before_mb = Round-Number (
                (Convert-Number $cycle.used_before_bytes) / 1MB)
            used_after_mb = Round-Number (
                (Convert-Number $cycle.used_after_bytes) / 1MB)
            old_mb = Round-Number (
                (Convert-Number $cycle.old_bytes) / 1MB)
            pinned_mb = Round-Number (
                (Convert-Number $cycle.pinned_bytes) / 1MB)
            stalls = [int]$cycle.allocation_stalls
            fallback = [int]$cycle.evacuation_fallback_regions
        }
    }

    $cycleNine = $gcCycles |
        Where-Object { [int]$_.cycle -eq 9 } |
        Select-Object -First 1
    $cycleNineAppSeconds = $cycleNine.app_ms / 1000

    return [ordered]@{
        name = $Name
        directory = [IO.Path]::GetFullPath($Directory)
        app_epoch_ms = Round-Number $appEpochMs
        totals = [ordered]@{
            frames = $frames.Count
            gc = $gcCycles.Count
            young = @(
                $gcCycles | Where-Object kind -eq 'young').Count
            full = @(
                $gcCycles | Where-Object kind -eq 'full').Count
            process = $processSamples.Count
            hardware = $hardwareSamples.Count
            max_cycle = (
                $gcCycles |
                    ForEach-Object { [int]$_.cycle } |
                    Measure-Object -Maximum
            ).Maximum
            responding_zero = @(
                $processSamples |
                    Where-Object {
                        (Convert-Number $_.responding) -eq 0
                    }).Count
            stalls = (
                $gcCycles |
                    ForEach-Object {
                        [int]$_.allocation_stalls
                    } |
                    Measure-Object -Sum
            ).Sum
            stall_timeouts = (
                $gcCycles |
                    ForEach-Object {
                        [int]$_.allocation_stall_timeouts
                    } |
                    Measure-Object -Sum
            ).Sum
            emergency = (
                $gcCycles |
                    ForEach-Object {
                        [int]$_.emergency_collections
                    } |
                    Measure-Object -Sum
            ).Sum
            evacuation_fallback = (
                $gcCycles |
                    ForEach-Object {
                        [int]$_.evacuation_fallback_regions
                    } |
                    Measure-Object -Sum
            ).Sum
            remembered_overflow = (
                $gcCycles |
                    ForEach-Object {
                        [int]$_.initial_remembered_overflow_regions +
                            [int]$_.final_remembered_overflow_regions
                    } |
                    Measure-Object -Sum
            ).Sum
        }
        full_cycles = @($fullCycles)
        warmup_35_50 = Get-Window 35 50
        stable_50_215 = Get-Window 50 215
        common_50_210 = Get-Window 50 210
        stable_60_215 = Get-Window 60 215
        common_60_210 = Get-Window 60 210
        phase_anchor = [ordered]@{
            cycle = 9
            app_s = Round-Number $cycleNineAppSeconds
        }
        phase_aligned_20_160_after_cycle9 = Get-Window (
            $cycleNineAppSeconds + 20) (
            $cycleNineAppSeconds + 160)
        mid_120_140 = Get-Window 120 140
        tail_215_223 = Get-Window 215 223
    }
}

@(
    Get-RunAnalysis 'default64' $BaselineDirectory
    Get-RunAnalysis 'candidate128' $CandidateDirectory
) | ConvertTo-Json -Depth 9
