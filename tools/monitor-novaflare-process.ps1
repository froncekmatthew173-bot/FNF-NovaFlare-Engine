[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [int]$TargetProcessId,

    [Parameter(Mandatory = $true)]
    [string]$RunDirectory,

    [ValidateRange(1, 3600)]
    [int]$MaximumSeconds = 480,

    [ValidateRange(50, 5000)]
    [int]$IntervalMilliseconds = 250
)

$ErrorActionPreference = 'Stop'
$runPath = [IO.Path]::GetFullPath($RunDirectory)
New-Item -ItemType Directory -Path $runPath -Force | Out-Null
$csvPath = Join-Path $runPath 'process-samples.csv'
$statusPath = Join-Path $runPath 'process-monitor-status.txt'
$logicalProcessors = [Math]::Max(1, [Environment]::ProcessorCount)
$started = [Diagnostics.Stopwatch]::StartNew()
$previousCpu = 0.0
$previousElapsed = 0.0
$sample = 0

if (-not ('NovaFlareProcessMonitorNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class NovaFlareProcessMonitorNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool IsIconic(IntPtr window);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr window, out RECT rect);

    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(
        IntPtr window, int attribute, out int value, int valueSize);
}
'@
}

$writer = [IO.StreamWriter]::new($csvPath, $false, [Text.UTF8Encoding]::new($false))
try {
    $writer.WriteLine(
        'sample,elapsed_ms,utc,responding,cpu_seconds,cpu_percent,' +
        'private_mb,working_set_mb,virtual_mb,handles,threads,paged_mb,' +
        'nonpaged_system_mb,main_window_handle,foreground,visible,iconic,' +
        'cloaked,window_left,window_top,window_width,window_height')
    $writer.Flush()

    while ($started.Elapsed.TotalSeconds -lt $MaximumSeconds) {
        try {
            $process = Get-Process -Id $TargetProcessId -ErrorAction Stop
            $process.Refresh()
        }
        catch {
            break
        }

        $sample++
        $elapsed = $started.Elapsed.TotalSeconds
        $cpu = if ($null -eq $process.CPU) { 0.0 } else { [double]$process.CPU }
        $elapsedDelta = $elapsed - $previousElapsed
        $cpuPercent = if ($sample -le 1 -or $elapsedDelta -le 0) {
            0.0
        }
        else {
            (($cpu - $previousCpu) / $elapsedDelta) * 100.0 / $logicalProcessors
        }

        $window = $process.MainWindowHandle
        $foregroundWindow =
            [NovaFlareProcessMonitorNative]::GetForegroundWindow()
        $isForeground = if ($window -eq [IntPtr]::Zero) {
            -1
        }
        else {
            [int]($foregroundWindow -eq $window)
        }
        $isVisible = if ($window -eq [IntPtr]::Zero) {
            -1
        }
        else {
            [int][NovaFlareProcessMonitorNative]::IsWindowVisible($window)
        }
        $isIconic = if ($window -eq [IntPtr]::Zero) {
            -1
        }
        else {
            [int][NovaFlareProcessMonitorNative]::IsIconic($window)
        }
        $isCloaked = -1
        $windowLeft = 0
        $windowTop = 0
        $windowWidth = 0
        $windowHeight = 0
        if ($window -ne [IntPtr]::Zero) {
            $cloakedValue = 0
            $dwmResult =
                [NovaFlareProcessMonitorNative]::DwmGetWindowAttribute(
                    $window, 14, [ref]$cloakedValue, 4)
            if ($dwmResult -eq 0) {
                $isCloaked = [int]($cloakedValue -ne 0)
            }

            $rect = New-Object NovaFlareProcessMonitorNative+RECT
            if (
                [NovaFlareProcessMonitorNative]::GetWindowRect(
                    $window, [ref]$rect)
            ) {
                $windowLeft = $rect.Left
                $windowTop = $rect.Top
                $windowWidth = $rect.Right - $rect.Left
                $windowHeight = $rect.Bottom - $rect.Top
            }
        }

        $row = @(
            $sample,
            [Math]::Round($started.Elapsed.TotalMilliseconds),
            [DateTime]::UtcNow.ToString('o'),
            [int][bool]$process.Responding,
            [Math]::Round($cpu, 4),
            [Math]::Round($cpuPercent, 2),
            [Math]::Round($process.PrivateMemorySize64 / 1MB, 2),
            [Math]::Round($process.WorkingSet64 / 1MB, 2),
            [Math]::Round($process.VirtualMemorySize64 / 1MB, 2),
            $process.HandleCount,
            $process.Threads.Count,
            [Math]::Round($process.PagedMemorySize64 / 1MB, 2),
            [Math]::Round($process.NonpagedSystemMemorySize64 / 1MB, 2),
            $window.ToInt64(),
            $isForeground,
            $isVisible,
            $isIconic,
            $isCloaked,
            $windowLeft,
            $windowTop,
            $windowWidth,
            $windowHeight
        )
        $writer.WriteLine(($row -join ','))
        $writer.Flush()
        $previousCpu = $cpu
        $previousElapsed = $elapsed
        Start-Sleep -Milliseconds $IntervalMilliseconds
    }

    $state = if (Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue) {
        'timeout-process-alive'
    }
    else {
        'process-exited'
    }
    [IO.File]::WriteAllText(
        $statusPath,
        "state=$state`r`nsamples=$sample`r`nelapsed_ms=$([Math]::Round($started.Elapsed.TotalMilliseconds))`r`n",
        [Text.UTF8Encoding]::new($false))
}
finally {
    $writer.Dispose()
}
