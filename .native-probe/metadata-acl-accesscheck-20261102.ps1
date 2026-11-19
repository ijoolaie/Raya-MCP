param([Parameter(Mandatory=$true)][string]$TargetPath)
$source = @'
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)]
public struct RayaGenericMapping { public uint GenericRead; public uint GenericWrite; public uint GenericExecute; public uint GenericAll; }
public static class RayaAccessCheckProbe {
 [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 static extern bool GetFileSecurityW(string fileName, uint requestedInformation, IntPtr sd, uint length, out uint needed);
 [DllImport("advapi32.dll", SetLastError=true)] static extern bool IsValidSecurityDescriptor(IntPtr sd);
 [DllImport("advapi32.dll", SetLastError=true)] static extern bool GetSecurityDescriptorDacl(IntPtr sd, out bool present, out IntPtr dacl, out bool defaulted);
 [DllImport("advapi32.dll", SetLastError=true)] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
 [DllImport("advapi32.dll", SetLastError=true)] static extern bool DuplicateToken(IntPtr existingToken, int level, out IntPtr duplicateToken);
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
 [DllImport("advapi32.dll", SetLastError=true)] static extern bool AccessCheck(IntPtr sd, IntPtr token, uint desired, ref RayaGenericMapping mapping, IntPtr privilegeSet, ref uint privilegeSetLength, out uint granted, [MarshalAs(UnmanagedType.Bool)] out bool allowed);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateFileW(string name, uint desired, uint share, IntPtr sa, uint disposition, uint flags, IntPtr templateFile);
 public static object Run(string path) {
  uint needed=0; GetFileSecurityW(path,7,IntPtr.Zero,0,out needed);
  int firstErr=Marshal.GetLastWin32Error();
  IntPtr sd=Marshal.AllocHGlobal((int)needed), primary=IntPtr.Zero, token=IntPtr.Zero, priv=Marshal.AllocHGlobal(4096);
  try {
   if(!GetFileSecurityW(path,7,sd,needed,out needed)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
   bool valid=IsValidSecurityDescriptor(sd), present, defaulted; IntPtr dacl;
   bool daclOk=GetSecurityDescriptorDacl(sd,out present,out dacl,out defaulted);
   if(!OpenProcessToken(GetCurrentProcess(),0x0002,out primary)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
   if(!DuplicateToken(primary,2,out token)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
   RayaGenericMapping map=new RayaGenericMapping { GenericRead=0x00120089, GenericWrite=0x00120116, GenericExecute=0x001200A0, GenericAll=0x001F01FF };
   uint[] masks={0x80,0x00100080,0x1,0x100,0x20000};
   string[] labels={"FILE_READ_ATTRIBUTES","FILE_READ_ATTRIBUTES|SYNCHRONIZE","FILE_READ_DATA","FILE_WRITE_ATTRIBUTES","READ_CONTROL"};
   object[] checks=new object[masks.Length];
   for(int i=0;i<masks.Length;i++) {
    uint len=4096, granted=0; bool allowed=false; Marshal.Copy(new byte[4096],0,priv,4096);
    bool ok=AccessCheck(sd,token,masks[i],ref map,priv,ref len,out granted,out allowed);
    checks[i]=new { right=labels[i], desiredAccess=String.Format("0x{0:X8}",masks[i]), accessCheckCallSucceeded=ok, accessAllowed=ok&&allowed, grantedAccess=String.Format("0x{0:X8}",granted), win32Error=ok?0:Marshal.GetLastWin32Error(), privilegeSetLength=len };
   }
   uint[] requests={0x80,0x00100080,0x1}; object[] opens=new object[requests.Length];
   for(int i=0;i<requests.Length;i++) {
    IntPtr h=CreateFileW(path,requests[i],7,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
    if(h==new IntPtr(-1)) opens[i]=new { desiredAccess=String.Format("0x{0:X8}",requests[i]), opened=false, win32Error=Marshal.GetLastWin32Error() };
    else opens[i]=new { desiredAccess=String.Format("0x{0:X8}",requests[i]), opened=true, closeSucceeded=CloseHandle(h) };
   }
   return new { descriptorValid=valid, daclQuerySucceeded=daclOk, daclPresent=present, daclPointerNonNull=dacl!=IntPtr.Zero, initialGetFileSecurityError=firstErr, securityDescriptorBytes=needed, accessChecks=checks, createFileWOpens=opens };
  } finally { if(token!=IntPtr.Zero) CloseHandle(token); if(primary!=IntPtr.Zero) CloseHandle(primary); Marshal.FreeHGlobal(priv); Marshal.FreeHGlobal(sd); }
 }
}
'@
Add-Type -TypeDefinition $source -Language CSharp
[RayaAccessCheckProbe]::Run($TargetPath) | ConvertTo-Json -Depth 8 -Compress
