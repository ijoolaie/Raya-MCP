param()
$ErrorActionPreference = 'Stop'
$fixture = Join-Path $env:TEMP ('raya-ntfs-rights-isolation-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$sid = ([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value
$systemSid = '*S-1-5-18'
$cases = @(
  [pscustomobject]@{name='rd-only.txt';grant='(RD)';label='FILE_READ_DATA only'},
  [pscustomobject]@{name='synchronize-only.txt';grant='(S)';label='SYNCHRONIZE only'},
  [pscustomobject]@{name='neither.txt';grant=$null;label='neither RD nor SYNCHRONIZE'}
)
try {
  foreach($c in $cases) {
    $c | Add-Member -NotePropertyName path -NotePropertyValue (Join-Path $fixture $c.name)
    Set-Content -LiteralPath $c.path -Value 'NTFS-RIGHTS-ISOLATION-CANARY' -NoNewline
    & icacls.exe $c.path /inheritance:r | Out-Null
    if($LASTEXITCODE -ne 0){throw "inheritance removal failed for $($c.name)"}
    if($c.grant) {
      & icacls.exe $c.path /grant:r ("*"+$sid+":"+$c.grant) ($systemSid+":(F)") | Out-Null
    } else {
      & icacls.exe $c.path /grant:r ($systemSid+":(F)") | Out-Null
    }
    if($LASTEXITCODE -ne 0){throw "grant setup failed for $($c.name)"}
    & icacls.exe $c.path /deny ("*"+$sid+":(RA)") | Out-Null
    if($LASTEXITCODE -ne 0){throw "deny setup failed for $($c.name)"}
  }
  $source = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)] public struct RayaMapIso { public uint GenericRead,GenericWrite,GenericExecute,GenericAll; }
[StructLayout(LayoutKind.Sequential)] public struct RayaBasicIso { public uint Attributes,GrantedAccess,HandleCount,PointerCount,PagedPoolUsage,NonPagedPoolUsage; [MarshalAs(UnmanagedType.ByValArray,SizeConst=3)] public uint[] Reserved; public uint NameInfoSize,TypeInfoSize,SecurityDescriptorSize; public long CreateTime; }
public static class RayaNtfsRightsIsolation {
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool DuplicateToken(IntPtr t,int l,out IntPtr d);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string p,uint a,uint s,IntPtr sa,uint c,uint f,IntPtr t);
 [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h,int cls,IntPtr b,uint n,out uint ret);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetNamedSecurityInfoW(string n,int t,uint i,out IntPtr o,out IntPtr g,out IntPtr d,out IntPtr s,out IntPtr sd);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AccessCheck(IntPtr sd,IntPtr token,uint desired,ref RayaMapIso map,IntPtr ps,ref uint pslen,out uint granted,out bool allowed);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr LocalFree(IntPtr p);
 static object Check(string path,IntPtr token,uint mask,string label) {
  IntPtr o,g,d,s,sd; uint ge=GetNamedSecurityInfoW(path,1,7,out o,out g,out d,out s,out sd);
  if(ge!=0)throw new Exception("GetNamedSecurityInfo "+ge);
  try {
   IntPtr ps=Marshal.AllocHGlobal(1024); Marshal.Copy(new byte[1024],0,ps,1024);
   try {uint len=1024,gr=0;bool al=false;RayaMapIso m=new RayaMapIso{GenericRead=0x00120089,GenericWrite=0x00120116,GenericExecute=0x001200A0,GenericAll=0x001F01FF};bool ok=AccessCheck(sd,token,mask,ref m,ps,ref len,out gr,out al);return new{label,accessCheckCallOk=ok,accessAllowed=ok&&al,granted=String.Format("0x{0:X8}",gr),error=ok?0:Marshal.GetLastWin32Error()};}
   finally{Marshal.FreeHGlobal(ps);}
  } finally{LocalFree(sd);}
 }
 static object Open(string path,uint mask) {
  IntPtr h=CreateFileW(path,mask,7,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
  if(h==new IntPtr(-1))return new{requested=String.Format("0x{0:X8}",mask),opened=false,error=Marshal.GetLastWin32Error()};
  try {int size=Marshal.SizeOf(typeof(RayaBasicIso));IntPtr b=Marshal.AllocHGlobal(size);try{uint ret;int st=NtQueryObject(h,0,b,(uint)size,out ret);RayaBasicIso bi=st==0?(RayaBasicIso)Marshal.PtrToStructure(b,typeof(RayaBasicIso)):new RayaBasicIso();return new{requested=String.Format("0x{0:X8}",mask),opened=true,queryStatus=String.Format("0x{0:X8}",st),grantedAccess=st==0?String.Format("0x{0:X8}",bi.GrantedAccess):null};}finally{Marshal.FreeHGlobal(b);}}
  finally{CloseHandle(h);}
 }
 public static object Run(string[] paths) {
  IntPtr primary=IntPtr.Zero,imp=IntPtr.Zero;
  if(!OpenProcessToken(GetCurrentProcess(),0x000A,out primary))throw new Exception("OpenProcessToken "+Marshal.GetLastWin32Error());
  try {
   if(!DuplicateToken(primary,2,out imp))throw new Exception("DuplicateToken "+Marshal.GetLastWin32Error());
   var rows=new List<object>();
   foreach(string p in paths) rows.Add(new{name=System.IO.Path.GetFileName(p),accessChecks=new[]{Check(p,imp,0x80,"FILE_READ_ATTRIBUTES"),Check(p,imp,0x1,"FILE_READ_DATA"),Check(p,imp,0x00100000,"SYNCHRONIZE"),Check(p,imp,0x00100001,"FILE_READ_DATA | SYNCHRONIZE")},opens=new[]{Open(p,0x80),Open(p,0x00100080)}});
   return rows.ToArray();
  } finally{if(imp!=IntPtr.Zero)CloseHandle(imp);if(primary!=IntPtr.Zero)CloseHandle(primary);}
 }
}
'@
  Add-Type -TypeDefinition $source -Language CSharp
  $acl = foreach($c in $cases) { [pscustomobject]@{file=$c.name;case=$c.label;acl=((& icacls.exe $c.path) -join [Environment]::NewLine)} }
  $probe = [RayaNtfsRightsIsolation]::Run([string[]]@($cases | ForEach-Object {$_.path}))
  [pscustomobject]@{userSid=$sid;integrity=(whoami /groups | Select-String 'S-1-16-').ToString().Trim();aclListings=$acl;probe=$probe;scope='three disposable temp NTFS files; inheritance disabled; explicit RA deny; fixture deletion removes test ACLs'} | ConvertTo-Json -Depth 12
}
finally {
  Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}