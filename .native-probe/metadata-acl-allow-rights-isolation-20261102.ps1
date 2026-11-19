$ErrorActionPreference='Stop'
$fixture=Join-Path $env:TEMP ('raya-ra-allow-isolation-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force|Out-Null
$sid=([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value
try {
$source=@'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)] public struct RayaRAOA { public uint Length; public IntPtr RootDirectory,ObjectName; public uint Attributes; public IntPtr SecurityDescriptor,SecurityQualityOfService; }
[StructLayout(LayoutKind.Sequential)] public struct RayaRAUS { public ushort Length,MaximumLength; public IntPtr Buffer; }
[StructLayout(LayoutKind.Sequential)] public struct RayaRAIOSB { public IntPtr StatusOrPointer; public UIntPtr Information; }
[StructLayout(LayoutKind.Sequential)] public struct RayaRAMap { public uint GenericRead,GenericWrite,GenericExecute,GenericAll; }
[StructLayout(LayoutKind.Sequential)] public struct RayaRABasic { public uint Attributes,GrantedAccess,HandleCount,PointerCount,PagedPoolUsage,NonPagedPoolUsage; [MarshalAs(UnmanagedType.ByValArray,SizeConst=3)] public uint[] Reserved; public uint NameInfoSize,TypeInfoSize,SecurityDescriptorSize; public long CreateTime; }
public static class RayaRAAllowIsolation {
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool ConvertStringSidToSidW(string s,out IntPtr sid);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool InitializeAcl(IntPtr acl,uint len,uint rev);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AddAccessDeniedAceEx(IntPtr acl,uint rev,uint flags,uint mask,IntPtr sid);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AddAccessAllowedAceEx(IntPtr acl,uint rev,uint flags,uint mask,IntPtr sid);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint SetNamedSecurityInfoW(string name,int type,uint info,IntPtr owner,IntPtr group,IntPtr dacl,IntPtr sacl);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetNamedSecurityInfoW(string n,int t,uint i,out IntPtr o,out IntPtr g,out IntPtr d,out IntPtr s,out IntPtr sd);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool DuplicateToken(IntPtr t,int l,out IntPtr d);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AccessCheck(IntPtr sd,IntPtr token,uint desired,ref RayaRAMap map,IntPtr ps,ref uint pslen,out uint granted,out bool allowed);
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("ntdll.dll")] static extern int NtCreateFile(out IntPtr h,uint desired,ref RayaRAOA oa,out RayaRAIOSB iosb,IntPtr alloc,uint attrs,uint share,uint disp,uint opts,IntPtr ea,uint eaLen);
 [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h,int cls,IntPtr b,uint n,out uint ret);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string p,uint a,uint s,IntPtr sa,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr LocalFree(IntPtr p);
 static void Must(bool b,string s){if(!b)throw new Exception(s+" Win32="+Marshal.GetLastWin32Error());}
 static void SetDacl(string file,string userSid,uint allowMask){
  IntPtr u=IntPtr.Zero,sys=IntPtr.Zero,acl=IntPtr.Zero;
  try{
   Must(ConvertStringSidToSidW(userSid,out u),"Convert user SID"); Must(ConvertStringSidToSidW("S-1-5-18",out sys),"Convert SYSTEM SID");
   acl=Marshal.AllocHGlobal(4096); Must(InitializeAcl(acl,4096,2),"InitializeAcl");
   Must(AddAccessDeniedAceEx(acl,2,0,0x80,u),"Add deny RA");
   if(allowMask!=0) Must(AddAccessAllowedAceEx(acl,2,0,allowMask,u),"Add allow ACE");
   Must(AddAccessAllowedAceEx(acl,2,0,0x001F01FF,sys),"Add SYSTEM full");
   uint e=SetNamedSecurityInfoW(file,1,0x80000004,u,IntPtr.Zero,acl,IntPtr.Zero); if(e!=0)throw new Exception("SetNamedSecurityInfo="+e);
  }finally{if(acl!=IntPtr.Zero)Marshal.FreeHGlobal(acl);if(u!=IntPtr.Zero)LocalFree(u);if(sys!=IntPtr.Zero)LocalFree(sys);}
 }
 static object Check(string file,IntPtr sd,IntPtr token,uint desired){
  IntPtr ps=Marshal.AllocHGlobal(1024); try{uint len=1024,gr=0;bool allowed=false;RayaRAMap map=new RayaRAMap{GenericRead=0x00120089,GenericWrite=0x00120116,GenericExecute=0x001200A0,GenericAll=0x001F01FF};bool ok=AccessCheck(sd,token,desired,ref map,ps,ref len,out gr,out allowed);return new{callOk=ok,allowed=ok&&allowed,granted=String.Format("0x{0:X8}",gr),error=ok?0:Marshal.GetLastWin32Error()};}finally{Marshal.FreeHGlobal(ps);}
 }
 static object OpenWin32(string file,uint desired){
  IntPtr h=CreateFileW(file,desired,7,IntPtr.Zero,3,0x00200000,IntPtr.Zero);
  if(h==new IntPtr(-1))return new{opened=false,error=Marshal.GetLastWin32Error(),grantedAccess=(string)null};
  try{int n=Marshal.SizeOf(typeof(RayaRABasic));IntPtr b=Marshal.AllocHGlobal(n);try{uint ret;int qs=NtQueryObject(h,0,b,(uint)n,out ret);RayaRABasic x=qs==0?(RayaRABasic)Marshal.PtrToStructure(b,typeof(RayaRABasic)):new RayaRABasic();return new{opened=true,error=0,queryObjectStatus=String.Format("0x{0:X8}",qs),grantedAccess=qs==0?String.Format("0x{0:X8}",x.GrantedAccess):null};}finally{Marshal.FreeHGlobal(b);}}finally{CloseHandle(h);}
 }
 static object Open(string file,uint desired){
  string nt="\\??\\"+file; IntPtr str=IntPtr.Zero,usPtr=IntPtr.Zero,h=IntPtr.Zero;
  try{
   str=Marshal.StringToHGlobalUni(nt);RayaRAUS us=new RayaRAUS{Length=(ushort)(nt.Length*2),MaximumLength=(ushort)(nt.Length*2),Buffer=str};usPtr=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(RayaRAUS)));Marshal.StructureToPtr(us,usPtr,false);
   RayaRAOA oa=new RayaRAOA{Length=(uint)Marshal.SizeOf(typeof(RayaRAOA)),ObjectName=usPtr,Attributes=0x40};RayaRAIOSB iosb;
   int st=NtCreateFile(out h,desired,ref oa,out iosb,IntPtr.Zero,0x80,7,1,0x00200040,IntPtr.Zero,0);
   if(st<0)return new{opened=false,status=String.Format("0x{0:X8}",st),grantedAccess=(string)null};
   int n=Marshal.SizeOf(typeof(RayaRABasic));IntPtr b=Marshal.AllocHGlobal(n);
   try{uint ret;int qs=NtQueryObject(h,0,b,(uint)n,out ret);RayaRABasic x=qs==0?(RayaRABasic)Marshal.PtrToStructure(b,typeof(RayaRABasic)):new RayaRABasic();return new{opened=true,status=String.Format("0x{0:X8}",st),queryObjectStatus=String.Format("0x{0:X8}",qs),grantedAccess=qs==0?String.Format("0x{0:X8}",x.GrantedAccess):null};}finally{Marshal.FreeHGlobal(b);}
  }finally{if(h!=IntPtr.Zero)CloseHandle(h);if(usPtr!=IntPtr.Zero)Marshal.FreeHGlobal(usPtr);if(str!=IntPtr.Zero)Marshal.FreeHGlobal(str);}
 }
 public static object Run(string dir,string sid){
  var cases=new[]{new{label="deny-ra-no-user-allow",mask=0u},new{label="deny-ra-allow-sync",mask=0x00100000u},new{label="deny-ra-allow-read-data",mask=0x00000001u},new{label="deny-ra-allow-read-control",mask=0x00020000u},new{label="deny-ra-allow-write-attributes",mask=0x00000100u},new{label="deny-ra-allow-read-attributes",mask=0x00000080u},new{label="deny-ra-allow-sync-and-read-data",mask=0x00100001u}};
  var rows=new List<object>();
  foreach(var c in cases){
   string f=System.IO.Path.Combine(dir,c.label+".bin");System.IO.File.WriteAllText(f,"RA-ALLOW-ISOLATION");SetDacl(f,sid,c.mask);
   IntPtr o,g,d,s,sd;uint ge=GetNamedSecurityInfoW(f,1,7,out o,out g,out d,out s,out sd);if(ge!=0)throw new Exception("GetNamedSecurityInfo="+ge);
   IntPtr primary=IntPtr.Zero,imp=IntPtr.Zero;
   try{Must(OpenProcessToken(GetCurrentProcess(),0x000A,out primary),"OpenProcessToken");Must(DuplicateToken(primary,2,out imp),"DuplicateToken");
    rows.Add(new{scenario=c.label,allowMask=String.Format("0x{0:X8}",c.mask),accessCheckRA=Check(f,sd,imp,0x80),win32CreateRA=OpenWin32(f,0x80),win32CreateRA_SYNC=OpenWin32(f,0x00100080),ntCreateRA=Open(f,0x80),ntCreateRA_SYNC=Open(f,0x00100080)});
   }finally{if(imp!=IntPtr.Zero)CloseHandle(imp);if(primary!=IntPtr.Zero)CloseHandle(primary);LocalFree(sd);}
  }
  return new{userSid=sid,results=rows};
 }
}
'@
Add-Type -TypeDefinition $source -Language CSharp
$result=[RayaRAAllowIsolation]::Run($fixture,$sid)
[pscustomobject]@{integrity=(whoami /groups|Select-String 'S-1-16-').ToString().Trim();userSid=$sid;results=$result;scope='seven disposable NTFS files; isolated DACL per file with explicit user DENY RA and one controlled optional user ALLOW ACE; SYSTEM full control; no persistent ACL changed'}|ConvertTo-Json -Depth 15
} finally { Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue }