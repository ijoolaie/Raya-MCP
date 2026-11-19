param()
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $env:TEMP ('raya-token-priv-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$file = Join-Path $fixture 'canary.txt'
Set-Content -LiteralPath $file -Value 'TOKEN-PRIVILEGE-CANARY' -NoNewline
$sid = ([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value
$denyApplied = $false
try {
  & icacls.exe $file /deny ("*" + $sid + ":(RA)") | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "icacls deny failed: $LASTEXITCODE" }
  $denyApplied = $true
  $aclText = (& icacls.exe $file) -join [Environment]::NewLine
  $source = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
[StructLayout(LayoutKind.Sequential)] public struct RayaLuid { public uint LowPart; public int HighPart; }
[StructLayout(LayoutKind.Sequential)] public struct RayaLuidAttr { public RayaLuid Luid; public uint Attributes; }
[StructLayout(LayoutKind.Sequential)] public struct RayaMapping { public uint GenericRead, GenericWrite, GenericExecute, GenericAll; }
[StructLayout(LayoutKind.Sequential)] public struct RayaBasicInfo {
 public uint Attributes, GrantedAccess, HandleCount, PointerCount, PagedPoolUsage, NonPagedPoolUsage;
 [MarshalAs(UnmanagedType.ByValArray, SizeConst=3)] public uint[] Reserved;
 public uint NameInfoSize, TypeInfoSize, SecurityDescriptorSize;
 public long CreateTime;
}
[StructLayout(LayoutKind.Sequential)] public struct RayaByHandleInfo {
 public uint FileAttributes; public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime, LastAccessTime, LastWriteTime;
 public uint VolumeSerialNumber, FileSizeHigh, FileSizeLow, NumberOfLinks, FileIndexHigh, FileIndexLow;
}
public static class RayaTokenPrivilegeProbe {
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint access,out IntPtr token);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool DuplicateToken(IntPtr token,int level,out IntPtr duplicate);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool GetTokenInformation(IntPtr token,int cls,IntPtr buffer,int length,out int needed);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool LookupPrivilegeNameW(string system,ref RayaLuid luid,StringBuilder name,ref int length);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string path,uint desired,uint share,IntPtr sa,uint disposition,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(IntPtr h,out RayaByHandleInfo info);
 [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h,int infoClass,IntPtr info,uint length,out uint returned);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AccessCheck(IntPtr sd,IntPtr token,uint desired,ref RayaMapping map,IntPtr priv,ref uint privLen,out uint granted,out bool allowed);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetNamedSecurityInfoW(string name,int type,uint info,out IntPtr owner,out IntPtr group,out IntPtr dacl,out IntPtr sacl,out IntPtr sd);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr LocalFree(IntPtr p);
 static string PrivName(RayaLuid luid) {
  int n=0; LookupPrivilegeNameW(null,ref luid,null,ref n);
  StringBuilder b=new StringBuilder(Math.Max(n,1)+2);
  if(!LookupPrivilegeNameW(null,ref luid,b,ref n)) return "LUID:"+luid.HighPart+":"+luid.LowPart;
  return b.ToString();
 }
 static object[] ReadPrivileges(IntPtr token) {
  int needed=0; GetTokenInformation(token,3,IntPtr.Zero,0,out needed);
  IntPtr b=Marshal.AllocHGlobal(needed);
  try {
   if(!GetTokenInformation(token,3,b,needed,out needed)) throw new Exception("GetTokenInformation(TokenPrivileges) "+Marshal.GetLastWin32Error());
   uint count=(uint)Marshal.ReadInt32(b); var list=new List<object>();
   int off=4, stride=Marshal.SizeOf(typeof(RayaLuidAttr));
   for(int i=0;i<count;i++) {
    RayaLuidAttr la=(RayaLuidAttr)Marshal.PtrToStructure(IntPtr.Add(b,off+i*stride),typeof(RayaLuidAttr));
    list.Add(new {name=PrivName(la.Luid),attributes=String.Format("0x{0:X8}",la.Attributes),enabled=(la.Attributes&2)!=0,enabledByDefault=(la.Attributes&1)!=0,removed=(la.Attributes&4)!=0,usedForAccess=(la.Attributes&0x80000000)!=0});
   }
   return list.ToArray();
  } finally { Marshal.FreeHGlobal(b); }
 }
 static object Check(IntPtr sd,IntPtr token,uint mask,string label) {
  IntPtr p=Marshal.AllocHGlobal(4096); Marshal.Copy(new byte[4096],0,p,4096);
  try {
   uint len=4096,g=0; bool a=false;
   RayaMapping m=new RayaMapping{GenericRead=0x00120089,GenericWrite=0x00120116,GenericExecute=0x001200A0,GenericAll=0x001F01FF};
   bool ok=AccessCheck(sd,token,mask,ref m,p,ref len,out g,out a);
   uint count=ok?(uint)Marshal.ReadInt32(p):0;
   var used=new List<object>(); int off=8;
   for(int i=0;i<count && i<32;i++) {
    RayaLuidAttr la=(RayaLuidAttr)Marshal.PtrToStructure(IntPtr.Add(p,off+i*Marshal.SizeOf(typeof(RayaLuidAttr))),typeof(RayaLuidAttr));
    used.Add(new{name=PrivName(la.Luid),attributes=String.Format("0x{0:X8}",la.Attributes)});
   }
   return new {right=label,ok,allowed=ok&&a,granted=String.Format("0x{0:X8}",g),error=ok?0:Marshal.GetLastWin32Error(),privilegeSetCount=count,privilegesUsed=used.ToArray()};
  } finally { Marshal.FreeHGlobal(p); }
 }
 static object OpenAndQuery(string path,uint mask) {
  IntPtr h=CreateFileW(path,mask,7,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
  if(h==new IntPtr(-1)) return new{mask=String.Format("0x{0:X8}",mask),opened=false,error=Marshal.GetLastWin32Error()};
  try {
   RayaBasicInfo bi=new RayaBasicInfo(); int size=Marshal.SizeOf(typeof(RayaBasicInfo)); IntPtr b=Marshal.AllocHGlobal(size);
   int status; uint returned;
   try { status=NtQueryObject(h,0,b,(uint)size,out returned); if(status==0) bi=(RayaBasicInfo)Marshal.PtrToStructure(b,typeof(RayaBasicInfo)); }
   finally { Marshal.FreeHGlobal(b); }
   RayaByHandleInfo fi; bool fiOk=GetFileInformationByHandle(h,out fi);
   return new{mask=String.Format("0x{0:X8}",mask),opened=true,ntQueryObjectStatus=String.Format("0x{0:X8}",status),queryReturned=returned,queryBufferSize=size,grantedAccess=status==0?String.Format("0x{0:X8}",bi.GrantedAccess):null,handleCount=status==0?bi.HandleCount:0,fileInfoOk=fiOk,fileIndex=fiOk?String.Format("{0:X8}{1:X8}",fi.FileIndexHigh,fi.FileIndexLow):null};
  } finally { CloseHandle(h); }
 }
 public static object Run(string path) {
  IntPtr owner,group,dacl,sacl,sd; uint e=GetNamedSecurityInfoW(path,1,7,out owner,out group,out dacl,out sacl,out sd);
  if(e!=0) throw new Exception("GetNamedSecurityInfo "+e);
  IntPtr primary=IntPtr.Zero,imp=IntPtr.Zero;
  try {
   if(!OpenProcessToken(GetCurrentProcess(),0x000A,out primary)) throw new Exception("OpenProcessToken "+Marshal.GetLastWin32Error());
   if(!DuplicateToken(primary,2,out imp)) throw new Exception("DuplicateToken "+Marshal.GetLastWin32Error());
   return new{
    processTokenPrivileges=ReadPrivileges(primary),
    impersonationTokenPrivileges=ReadPrivileges(imp),
    accessChecks=new[]{Check(sd,imp,0x80,"FILE_READ_ATTRIBUTES"),Check(sd,imp,0x00020080,"FILE_READ_ATTRIBUTES|READ_CONTROL"),Check(sd,imp,0x1,"FILE_READ_DATA")},
    opens=new[]{OpenAndQuery(path,0x80),OpenAndQuery(path,0x00020080)}
   };
  } finally { if(imp!=IntPtr.Zero)CloseHandle(imp); if(primary!=IntPtr.Zero)CloseHandle(primary); if(sd!=IntPtr.Zero)LocalFree(sd); }
 }
}
'@
  Add-Type -TypeDefinition $source -Language CSharp
  $probe = [RayaTokenPrivilegeProbe]::Run($file)
  [pscustomobject]@{userSid=$sid;integrity=(whoami /groups | Select-String 'S-1-16-').ToString().Trim();aclListing=$aclText;probe=$probe;scope='disposable temp file only; explicit deny removed in finally'} | ConvertTo-Json -Depth 12
}
finally {
  if ($denyApplied) { & icacls.exe $file /remove:d ("*" + $sid) | Out-Null }
  Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}