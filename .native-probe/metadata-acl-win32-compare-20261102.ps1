param([Parameter(Mandatory=$true)][string]$TargetPath)
$source = @'
using System;
using System.Runtime.InteropServices;

public static class RayaAclWin32Probe {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr CreateFileW(
        string fileName, uint desiredAccess, uint shareMode,
        IntPtr securityAttributes, uint creationDisposition,
        uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CloseHandle(IntPtr handle);

    public static object Probe(string path, uint access) {
        IntPtr h = CreateFileW(path, access, 7, IntPtr.Zero, 3, 0x00200000, IntPtr.Zero);
        if (h == new IntPtr(-1)) {
            int error = Marshal.GetLastWin32Error();
            return new { desiredAccess = String.Format("0x{0:X8}", access), opened = false, win32Error = error };
        }
        bool closed = CloseHandle(h);
        return new { desiredAccess = String.Format("0x{0:X8}", access), opened = true, closeSucceeded = closed };
    }
}
'@
Add-Type -TypeDefinition $source -Language CSharp
$results = @(
  [RayaAclWin32Probe]::Probe($TargetPath, 0x00000080),
  [RayaAclWin32Probe]::Probe($TargetPath, 0x00100080),
  [RayaAclWin32Probe]::Probe($TargetPath, 0x00000001)
)
[pscustomobject]@{ target = $TargetPath; results = $results } | ConvertTo-Json -Depth 4 -Compress
