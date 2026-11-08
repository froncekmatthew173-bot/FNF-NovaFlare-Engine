[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [int]$TargetProcessId,

    [Parameter(Mandatory = $true)]
    [string]$RunDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Name,

    [ValidateSet('None', 'Enter', 'Left', 'Right', 'Down', 'Up', 'S', 'W', 'Home', 'End', 'R', 'Minus', 'NumpadMinus', 'MouseLeft', 'Escape')]
    [string]$Key = 'None',

    [ValidateRange(1, 2000)]
    [int]$KeyHoldMilliseconds = 35,

    [ValidateRange(1, 200)]
    [int]$Repeat = 1,

    [ValidateRange(0, 2000)]
    [int]$RepeatIntervalMilliseconds = 80,

    [ValidateSet('PrintWindow', 'Screen')]
    [string]$CaptureMethod = 'PrintWindow',

    [int]$MouseX = 0,

    [int]$MouseY = 0,

    [int]$PostKeyDelayMilliseconds = 0
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class NovaFlareWindowProbeNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint flags);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern uint MapVirtualKey(uint code, uint mapType);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
}
'@

$process = Get-Process -Id $TargetProcessId -ErrorAction Stop
$process.Refresh()
$window = $process.MainWindowHandle
if ($window -eq [IntPtr]::Zero) {
    throw "Process $TargetProcessId does not have a main window yet"
}

if ($Key -ne 'None' -or $CaptureMethod -eq 'Screen') {
    [NovaFlareWindowProbeNative]::SetForegroundWindow($window) | Out-Null
    Start-Sleep -Milliseconds 75
}

if ($Key -ne 'None') {
    for ($inputIndex = 0; $inputIndex -lt $Repeat; $inputIndex++) {
      if ($Key -eq 'MouseLeft') {
        $inputRect = New-Object NovaFlareWindowProbeNative+RECT
        if (-not [NovaFlareWindowProbeNative]::GetWindowRect($window, [ref]$inputRect)) {
            throw "GetWindowRect failed before mouse input for process $TargetProcessId"
        }
        [NovaFlareWindowProbeNative]::SetCursorPos(
            $inputRect.Left + $MouseX, $inputRect.Top + $MouseY) | Out-Null
        Start-Sleep -Milliseconds 75
        [NovaFlareWindowProbeNative]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds $KeyHoldMilliseconds
        [NovaFlareWindowProbeNative]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
      }
      else {
        $virtualKeys = @{
            Enter  = 0x0D
            Left   = 0x25
            Right  = 0x27
            Down   = 0x28
            Up     = 0x26
            S      = 0x53
            W      = 0x57
            Home   = 0x24
            End    = 0x23
            R      = 0x52
            Minus  = 0xBD
            NumpadMinus = 0x6D
            Escape = 0x1B
        }
        $scanCode = [byte][NovaFlareWindowProbeNative]::MapVirtualKey([uint32]$virtualKeys[$Key], 0)
        if ([NovaFlareWindowProbeNative]::GetForegroundWindow() -eq $window) {
            # Lime polls the physical keyboard state in some states (notably
            # the Freeplay chart browser), so prefer a real foreground key
            # transition whenever an interactive desktop is available.
            [NovaFlareWindowProbeNative]::keybd_event(
                [byte]$virtualKeys[$Key], $scanCode, 0, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds $KeyHoldMilliseconds
            [NovaFlareWindowProbeNative]::keybd_event(
                [byte]$virtualKeys[$Key], $scanCode, 0x0002, [UIntPtr]::Zero)
        }
        else {
            # A headless/locked console reports no foreground HWND. Deliver a
            # complete Win32 key transition to the Lime window so automated
            # route tests can continue without changing the game runtime.
            $keyDownLParam = [IntPtr]([int64](1 -bor ([int64]$scanCode -shl 16)))
            $keyUpLParam = [IntPtr]([int64](1 -bor ([int64]$scanCode -shl 16) -bor 0xC0000000L))
            [NovaFlareWindowProbeNative]::PostMessage(
                $window, 0x0100, [IntPtr]$virtualKeys[$Key], $keyDownLParam) | Out-Null
            Start-Sleep -Milliseconds $KeyHoldMilliseconds
            [NovaFlareWindowProbeNative]::PostMessage(
                $window, 0x0101, [IntPtr]$virtualKeys[$Key], $keyUpLParam) | Out-Null
        }
      }
      if ($inputIndex + 1 -lt $Repeat -and $RepeatIntervalMilliseconds -gt 0) {
          Start-Sleep -Milliseconds $RepeatIntervalMilliseconds
      }
    }
}

if ($PostKeyDelayMilliseconds -gt 0) {
    Start-Sleep -Milliseconds $PostKeyDelayMilliseconds
}

$rect = New-Object NovaFlareWindowProbeNative+RECT
if (-not [NovaFlareWindowProbeNative]::GetWindowRect($window, [ref]$rect)) {
    throw "GetWindowRect failed for process $TargetProcessId"
}

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) {
    throw "Invalid game window bounds ${width}x${height}"
}

New-Item -ItemType Directory -Path $RunDirectory -Force | Out-Null
$path = Join-Path $RunDirectory ($Name + '.png')
$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
if ($CaptureMethod -eq 'Screen') {
    # Capture the DWM-composed desktop pixels. PrintWindow can ask an OpenGL
    # window to paint while it is between draw calls, which looks like large
    # transient black rectangles even though the displayed frame is intact.
    $graphics.CopyFromScreen(
        $rect.Left, $rect.Top, 0, 0,
        (New-Object System.Drawing.Size($width, $height)),
        [System.Drawing.CopyPixelOperation]::SourceCopy)
}
else {
    $hdc = $graphics.GetHdc()
    try {
        if (-not [NovaFlareWindowProbeNative]::PrintWindow($window, $hdc, 2)) {
            throw "PrintWindow failed for process $TargetProcessId"
        }
    }
    finally {
        $graphics.ReleaseHdc($hdc)
    }
}
$graphics.Dispose()

try {
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $bitmap.Dispose()
}

[pscustomobject]@{
    ProcessId = $TargetProcessId
    WindowHandle = $window
    ForegroundWindow = [NovaFlareWindowProbeNative]::GetForegroundWindow()
    Key = $Key
    Repeat = $Repeat
    CaptureMethod = $CaptureMethod
    Screenshot = (Resolve-Path -LiteralPath $path).Path
    Width = $width
    Height = $height
}
