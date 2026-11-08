param(
    [Parameter(Mandatory = $true)]
    [int]$TargetProcessId,
    [int]$SampleCount = 24,
    [int]$IntervalMilliseconds = 500,
    [int64]$UpdateFpsRva = 0x552fe58,
    [int64]$DrawFpsRva = 0x552fe38,
    [int64]$UpdateFrameTimeRva = 0x552fe50,
    [int64]$DrawFrameTimeRva = 0x552fe30,
    [int64]$UpdateTargetRva = 0x55145dc,
    [int64]$DrawTargetRva = 0x55145d8
)

$ErrorActionPreference = 'Stop'

if (-not ('NovaFlarePerformance.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace NovaFlarePerformance {
    public static class NativeMethods {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(uint access, bool inheritHandle, int processId);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool ReadProcessMemory(
            IntPtr process,
            IntPtr address,
            byte[] buffer,
            int size,
            out IntPtr bytesRead);

        [DllImport("kernel32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr handle);
    }
}
'@
}

$process = Get-Process -Id $TargetProcessId
$baseAddress = $process.MainModule.BaseAddress.ToInt64()
$handle = [NovaFlarePerformance.NativeMethods]::OpenProcess(0x410, $false, $TargetProcessId)
if ($handle -eq [IntPtr]::Zero) {
    throw "OpenProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}

function Read-ProcessBytes([int64]$rva, [int]$count) {
    $buffer = [byte[]]::new($count)
    $bytesRead = [IntPtr]::Zero
    $address = [IntPtr]::new($baseAddress + $rva)
    if (-not [NovaFlarePerformance.NativeMethods]::ReadProcessMemory(
        $handle, $address, $buffer, $count, [ref]$bytesRead)) {
        throw "ReadProcessMemory failed at RVA 0x$($rva.ToString('x')): $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    return $buffer
}

function Read-ProcessDouble([int64]$rva) {
    return [BitConverter]::ToDouble((Read-ProcessBytes $rva 8), 0)
}

function Read-ProcessInt32([int64]$rva) {
    return [BitConverter]::ToInt32((Read-ProcessBytes $rva 4), 0)
}

try {
    $rows = for ($index = 0; $index -lt $SampleCount; $index++) {
        [pscustomobject]@{
            Sample = $index + 1
            TPS = [Math]::Round((Read-ProcessDouble $UpdateFpsRva), 1)
            FPS = [Math]::Round((Read-ProcessDouble $DrawFpsRva), 1)
            UpdateMs = [Math]::Round((Read-ProcessDouble $UpdateFrameTimeRva), 3)
            DrawMs = [Math]::Round((Read-ProcessDouble $DrawFrameTimeRva), 3)
        }
        if ($index + 1 -lt $SampleCount) {
            Start-Sleep -Milliseconds $IntervalMilliseconds
        }
    }

    $rows | Format-Table -AutoSize

    $tps = $rows.TPS | Where-Object { $_ -gt 0 }
    $fps = $rows.FPS | Where-Object { $_ -gt 0 }
    [pscustomobject]@{
        ProcessId = $TargetProcessId
        UpdateTarget = Read-ProcessInt32 $UpdateTargetRva
        DrawTarget = Read-ProcessInt32 $DrawTargetRva
        Samples = $rows.Count
        TPSMin = [Math]::Round(($tps | Measure-Object -Minimum).Minimum, 1)
        TPSAverage = [Math]::Round(($tps | Measure-Object -Average).Average, 1)
        TPSMax = [Math]::Round(($tps | Measure-Object -Maximum).Maximum, 1)
        FPSMin = [Math]::Round(($fps | Measure-Object -Minimum).Minimum, 1)
        FPSAverage = [Math]::Round(($fps | Measure-Object -Average).Average, 1)
        FPSMax = [Math]::Round(($fps | Measure-Object -Maximum).Maximum, 1)
    } | Format-List
}
finally {
    [void][NovaFlarePerformance.NativeMethods]::CloseHandle($handle)
}
