[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [int]$TargetProcessId,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [ValidateSet('Triage', 'Full')]
    [string]$DumpType = 'Full'
)

$ErrorActionPreference = 'Stop'
$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
$parent = Split-Path -Parent $resolvedOutput
New-Item -ItemType Directory -Path $parent -Force | Out-Null

if (-not ('NovaFlareDiagnostics.DumpWriter' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace NovaFlareDiagnostics {
    [Flags]
    public enum MiniDumpType : uint {
        Normal = 0x00000000,
        WithDataSegs = 0x00000001,
        WithFullMemory = 0x00000002,
        WithHandleData = 0x00000004,
        WithUnloadedModules = 0x00000020,
        WithProcessThreadData = 0x00000100,
        WithFullMemoryInfo = 0x00000800,
        WithThreadInfo = 0x00001000,
        WithTokenInformation = 0x00040000
    }

    public static class DumpWriter {
        [DllImport("Dbghelp.dll", SetLastError = true)]
        private static extern bool MiniDumpWriteDump(
            IntPtr process, uint processId, IntPtr file,
            MiniDumpType dumpType, IntPtr exception,
            IntPtr userStream, IntPtr callback);

        public static void Write(int processId, string output, bool full) {
            using (Process process = Process.GetProcessById(processId))
            using (FileStream stream = new FileStream(
                output, FileMode.Create, FileAccess.ReadWrite, FileShare.None)) {
                MiniDumpType type =
                    MiniDumpType.WithHandleData |
                    MiniDumpType.WithUnloadedModules |
                    MiniDumpType.WithProcessThreadData |
                    MiniDumpType.WithThreadInfo |
                    MiniDumpType.WithTokenInformation;
                if (full) {
                    type |= MiniDumpType.WithFullMemory |
                            MiniDumpType.WithFullMemoryInfo;
                } else {
                    type |= MiniDumpType.WithDataSegs;
                }
                if (!MiniDumpWriteDump(
                    process.Handle, unchecked((uint)processId),
                    stream.SafeFileHandle.DangerousGetHandle(), type,
                    IntPtr.Zero, IntPtr.Zero, IntPtr.Zero)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
            }
        }
    }
}
'@
}

[NovaFlareDiagnostics.DumpWriter]::Write(
    $TargetProcessId, $resolvedOutput, $DumpType -eq 'Full')
$item = Get-Item -LiteralPath $resolvedOutput
[pscustomobject]@{
    path = $item.FullName
    bytes = $item.Length
    dump_type = $DumpType
    process_id = $TargetProcessId
}
