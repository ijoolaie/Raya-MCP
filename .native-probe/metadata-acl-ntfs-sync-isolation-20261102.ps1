param()
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $env:TEMP ('raya-ntfs-sync-isolation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$sid = ([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value
$cases = @(
  [pscustomobject]@{name='rd-only.txt';mask=[uint32]0x00000001;label='RD only'},
  [pscustomobject]@{name='sync-only.txt';mask=[uint32]0x00100000;label='SYNCHRONIZE only'},
  [pscustomobject]@{name='rd-sync.txt';mask=[uint32]0x00100001;label='RD | SYNCHRONIZE'}
)
try {
  foreach($c in $cases) {
    $c | Add-Member -NotePropertyName path -NotePropertyValue (Join-Path $fixture $c.name)
    Set-Content -LiteralPath $c.path -Value 'NTFS-SYNCHRONIZE-ISOLATION-CANARY' -NoNewline
  }
  $source = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)] public struct RayaMapSync { public uint GenericRead,GenericWrite,GenericExecute,GenericAll; }
[StructLayout(LayoutKind.Sequential)] public struct RayaBasicSync { public uint Attributes,GrantedAccess,HandleCount,PointerCount,PagedPoolUsage,NonPagedPoolUsage; [MarshalAs(UnmanagedType.ByValArray,SizeConst=3)] public uint[] Reserved; public uint NameInfoSize,TypeInfoSize,SecurityDescriptorSize; public long CreateTime; }
public static class RayaNtfsSyncIsolation {
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool ConvertStringSidToSidW(string s,out IntPtr sid);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool InitializeAcl(IntPtr acl,uint len,uint rev);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AddAccessDeniedAceEx(IntPtr acl,uint rev,uint flags,uint mask,IntPtr sid);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AddAccessAllowedAceEx(IntPtr acl,uint rev,uint flags,uint mask,IntPtr sid);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint SetNamedSecurityInfoW(string name,int type,uint info,IntPtr owner,IntPtr group,IntPtr dacl,IntPtr sacl);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetNamedSecurityInfoW(string n,int t,uint i,out IntPtr o,out IntPtr g,out IntPtr d,out IntPtr s,out IntPtr sd);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool DuplicateToken(IntPtr t,int l,out IntPtr d);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AccessCheck(IntPtr sd,IntPtr token,uint desired,ref RayaMapSync map,IntPtr ps,ref uint pslen,out uint granted,out bool allowed);
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string p,uint a,uint s,IntPtr sa,uint c,uint f,IntPtr t);
 [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h,int cls,IntPtr b,uint n,out uint ret);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr LocalFree(IntPtr p);
 static void Must(bool ok,string what){if(!ok)throw new Exception(what+" "+Marshal.GetLastWin32Error());}
 static void SetAcl(string path,string userSid,uint allowMask) {
  IntPtr user=IntPtr.Zero,system=IntPtr.Zero,acl=IntPtr.Zero;
  try {
   Must(ConvertStringSidToSidW(userSid,out user),"Convert user SID");
   Must(ConvertStringSidToSidW("S-1-5-18",out system),"Convert SYSTEM SID");
   acl=Marshal.AllocHGlobal(4096); Must(InitializeAcl(acl,4096,2),"InitializeAcl");
   Must(AddAccessDeniedAceEx(acl,2,0,0x80,user),"Add deny RA");
   if(allowMask!=0) Must(AddAccessAllowedAceEx(acl,2,0,allowMask,user),"Add user allow");
   Must(AddAccessAllowedAceEx(acl,2,0,0x001F01FF,system),"Add SYSTEM full");
   uint e=SetNamedSecurityInfoW(path,1,0x80000004,user,IntPtr.Zero,acl,IntPtr.Zero);
   if(e!=0)throw new Exception("SetNamedSecurityInfo "+e);
  } finally {if(acl!=IntPtr.Zero)Marshal.FreeHGlobal(acl);if(user!=IntPtr.Zero)LocalFree(user);if(system!=IntPtr.Zero)LocalFree(system);}
 }
 static object Check(string path,IntPtr token,uint mask,string label) {
  IntPtr o,g,d,s,sd; uint ge=GetNamedSecurityInfoW(path,1,7,out o,out g,out d,out s,out sd);
  if(ge!=0)throw new Exception("GetNamedSecurityInfo "+ge);
  try {IntPtr ps=Marshal.AllocHGlobal(1024);Marshal.Copy(new byte[1024],0,ps,1024);
   try {uint len=1024,gr=0;bool al=false;RayaMapSync m=new RayaMapSync{GenericRead=0x00120089,GenericWrite=0x00120116,GenericExecute=0x001200A0,GenericAll=0x001F01FF};bool ok=AccessCheck(sd,token,mask,ref m,ps,ref len,out gr,out al);return new{label,accessCheckCallOk=ok,accessAllowed=ok&&al,granted=String.Format("0x{0:X8}",gr),error=ok?0:Marshal.GetLastWin32Error()};}
   finally{Marshal.FreeHGlobal(ps);}
  } finally{LocalFree(sd);}
 }
 static object Open(string path,uint mask) {
  IntPtr h=CreateFileW(path,mask,7,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
  if(h==new IntPtr(-1))return new{requested=String.Format("0x{0:X8}",mask),opened=false,error=Marshal.GetLastWin32Error()};
  try {int size=Marshal.SizeOf(typeof(RayaBasicSync));IntPtr b=Marshal.AllocHGlobal(size);try{uint ret;int st=NtQueryObject(h,0,b,(uint)size,out ret);RayaBasicSync bi=st==0?(RayaBasicSync)Marshal.PtrToStructure(b,typeof(RayaBasicSync)):new RayaBasicSync();return new{requested=String.Format("0x{0:X8}",mask),opened=true,queryStatus=String.Format("0x{0:X8}",st),grantedAccess=st==0?String.Format("0x{0:X8}",bi.GrantedAccess):null};}finally{Marshal.FreeHGlobal(b);}}
  finally{CloseHandle(h);}
 }
 public static object Run(string[] paths,uint[] masks,string sid) {
  for(int i=0;i<paths.Length;i++)SetAcl(paths[i],sid,masks[i]);
  IntPtr primary=IntPtr.Zero,imp=IntPtr.Zero;
  if(!OpenProcessToken(GetCurrentProcess(),0x000A,out primary))throw new Exception("OpenProcessToken "+Marshal.GetLastWin32Error());
  try {if(!DuplicateToken(primary,2,out imp))throw new Exception("DuplicateToken "+Marshal.GetLastWin32Error());
   var rows=new List<object>();
   foreach(string p in paths) rows.Add(new{name=System.IO.Path.GetFileName(p),accessChecks=new[]{Check(p,imp,0x80,"FILE_READ_ATTRIBUTES"),Check(p,imp,0x1,"FILE_READ_DATA"),Check(p,imp,0x00100000,"SYNCHRONIZE")},opens=new[]{Open(p,0x80),Open(p,0x00100080)}});
   return rows.ToArray();
  } finally{if(imp!=IntPtr.Zero)CloseHandle(imp);if(primary!=IntPtr.Zero)CloseHandle(primary);}
 }
}
'@
  Add-Type -TypeDefinition $source -Language CSharp
  $probe = [RayaNtfsSyncIsolation]::Run([string[]]@($cases | ForEach-Object {$_.path}),[uint32[]]@($cases | ForEach-Object {$_.mask}),$sid)
  $acl = foreach($c in $cases) { [pscustomobject]@{file=$c.name;case=$c.label;acl=((& icacls.exe $c.path) -join [Environment]::NewLine)} }
  [pscustomobject]@{userSid=$sid;integrity=(whoami /groups | Select-String 'S-1-16-').ToString().Trim();aclListings=$acl;probe=$probe;scope='three disposable temp NTFS files; DACLs constructed natively with explicit masks; fixture deletion removes test ACLs'} | ConvertTo-Json -Depth 12
}
finally {
  Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}