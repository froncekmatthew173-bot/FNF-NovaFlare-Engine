[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [int]$TargetProcessId,

    [Parameter(Mandatory = $true)]
    [string]$RunDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Name,

    [ValidateRange(500, 15000)]
    [int]$DurationMilliseconds = 4000,

    [ValidateRange(5, 95)]
    [int]$XPercent = 82,

    [switch]$Drag
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

public static class NovaFlareMouseMotionNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr window, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extraInfo);

    public static Task<int> MoveAsync(IntPtr window, int durationMilliseconds, int xPercent, bool drag)
    {
        return Task.Run(() => {
            RECT rect;
            if (!GetWindowRect(window, out rect))
                throw new InvalidOperationException("GetWindowRect failed");

            SetForegroundWindow(window);
            int x = rect.Left + (int)((rect.Right - rect.Left) * (xPercent / 100.0));
            int top = rect.Top + 150;
            int bottom = rect.Bottom - 90;
            int span = Math.Max(80, bottom - top);
            SetCursorPos(x, top);
            Thread.Sleep(80);
            if (drag)
                mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);

            int started = Environment.TickCount;
            int moves = 0;
            try {
                while (unchecked(Environment.TickCount - started) < durationMilliseconds)
                {
                    int phase = moves % 120;
                    double unit = phase < 60 ? phase / 59.0 : (119 - phase) / 59.0;
                    SetCursorPos(x, top + (int)(span * unit));
                    moves++;
                    Thread.Sleep(8);
                }
            }
            finally {
                if (drag)
                    mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
            }
            return moves;
        });
    }
}
'@

$process = Get-Process -Id $TargetProcessId -ErrorAction Stop
$process.Refresh()
$window = $process.MainWindowHandle
if ($window -eq [IntPtr]::Zero) {
    throw "Process $TargetProcessId does not have a main window"
}

$rect = New-Object NovaFlareMouseMotionNative+RECT
if (-not [NovaFlareMouseMotionNative]::GetWindowRect($window, [ref]$rect)) {
    throw 'GetWindowRect failed before motion probe'
}

New-Item -ItemType Directory -Path $RunDirectory -Force | Out-Null
$screenshot = Join-Path $RunDirectory ($Name + '.png')
$cpuStart = $process.TotalProcessorTime.TotalMilliseconds
$stopwatch = [Diagnostics.Stopwatch]::StartNew()
$motion = [NovaFlareMouseMotionNative]::MoveAsync($window, $DurationMilliseconds, $XPercent, $Drag.IsPresent)

Start-Sleep -Milliseconds ([Math]::Max(200, [int]($DurationMilliseconds / 2)))
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object Drawing.Bitmap $width, $height
$graphics = [Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($screenshot, [Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

$moves = $motion.GetAwaiter().GetResult()
$stopwatch.Stop()
$process.Refresh()
$cpuMilliseconds = $process.TotalProcessorTime.TotalMilliseconds - $cpuStart

[pscustomobject]@{
    ProcessId = $TargetProcessId
    Drag = $Drag.IsPresent
    XPercent = $XPercent
    Moves = $moves
    WallMilliseconds = [Math]::Round($stopwatch.Elapsed.TotalMilliseconds)
    CpuMilliseconds = [Math]::Round($cpuMilliseconds)
    OneCorePercent = [Math]::Round(100 * $cpuMilliseconds / $stopwatch.Elapsed.TotalMilliseconds, 1)
    Screenshot = (Resolve-Path -LiteralPath $screenshot).Path
}
