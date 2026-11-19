$ErrorActionPreference = 'Stop'
$fixture = Join-Path $env:TEMP ('raya-ra-abs-rel-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$sid = ([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value
try {
$source = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)] public struct RayaAROA { public uint Length; public IntPtr RootDirectory,ObjectName; public uint Attributes; public IntPtr SecurityDescriptor,SecurityQualityOfService; }
[StructLayout(LayoutKind.Sequential)] public struct RayaARUS { public ushort Length,MaximumLength; public IntPtr Buffer; }
[StructLayout(LayoutKind.Sequential)] public struct RayaARIOSB { public IntPtr StatusOrPointer; public UIntPtr Information; }
[StructLayout(LayoutKind.Sequential)] public struct RayaARMAP { public uint GenericRead,GenericWrite,GenericExecute,GenericAll; }
[StructLayout(LayoutKind.Sequential)] public struct RayaARBASIC { public uint Attributes,GrantedAccess,HandleCount,PointerCount,PagedPoolUsage,NonPagedPoolUsage; [MarshalAs(UnmanagedType.ByValArray,SizeConst=3)] public uint[] Reserved; public uint NameInfoSize,TypeInfoSize,SecurityDescriptorSize; public long CreateTime; }
public static class RayaAbsoluteRelativeAcl {
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool ConvertStringSidToSidW(string s,out IntPtr sid);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool InitializeAcl(IntPtr acl,uint len,uint rev);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AddAccessDeniedAceEx(IntPtr acl,uint rev,uint flags,uint mask,IntPtr sid);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AddAccessAllowedAceEx(IntPtr acl,uint rev,uint flags,uint mask,IntPtr sid);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint SetNamedSecurityInfoW(string name,int type,uint info,IntPtr owner,IntPtr group,IntPtr dacl,IntPtr sacl);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetNamedSecurityInfoW(string n,int t,uint i,out IntPtr o,out IntPtr g,out IntPtr d,out IntPtr s,out IntPtr sd);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool DuplicateToken(IntPtr t,int l,out IntPtr d);
 [DllImport("advapi32.dll",SetLastError=true)] static extern bool AccessCheck(IntPtr sd,IntPtr token,uint desired,ref RayaARMAP map,IntPtr ps,ref uint pslen,out uint granted,out bool allowed);
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("ntdll.dll")] static extern int NtCreateFile(out IntPtr h,uint desired,ref RayaAROA oa,out RayaARIOSB iosb,IntPtr alloc,uint attrs,uint share,uint disp,uint opts,IntPtr ea,uint eaLen);
 [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h,int cls,IntPtr b,uint n,out uint ret);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string p,uint a,uint s,IntPtr sa,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr LocalFree(IntPtr p);
 static void Must(bool b,string s){if(!b)throw new Exception(s+" Win32="+Marshal.GetLastWin32Error());}
 static void SetDacl(string file,string userSid){
  IntPtr u=IntPtr.Zero,sys=IntPtr.Zero,acl=IntPtr.Zero;
  try{
   Must(ConvertStringSidToSidW(userSid,out u),"Convert user SID"); Must(ConvertStringSidToSidW("S-1-5-18",out sys),"Convert SYSTEM SID");
   acl=Marshal.AllocHGlobal(4096); Must(InitializeAcl(acl,4096,2),"InitializeAcl");
   Must(AddAccessDeniedAceEx(acl,2,0,0x80,u),"Add deny FILE_READ_ATTRIBUTES");
   Must(AddAccessAllowedAceEx(acl,2,0,0x00100000,u),"Add allow SYNCHRONIZE");
   Must(AddAccessAllowedAceEx(acl,2,0,0x001F01FF,sys),"Add SYSTEM full");
   uint e=SetNamedSecurityInfoW(file,1,0x80000004,u,IntPtr.Zero,acl,IntPtr.Zero); if(e!=0)throw new Exception("SetNamedSecurityInfo="+e);
  }finally{if(acl!=IntPtr.Zero)Marshal.FreeHGlobal(acl);if(u!=IntPtr.Zero)LocalFree(u);if(sys!=IntPtr.Zero)LocalFree(sys);}
 }
 static object CheckRA(IntPtr sd,IntPtr token){
  IntPtr ps=Marshal.AllocHGlobal(1024);
  try{uint len=1024,gr=0;bool allowed=false;RayaARMAP map=new RayaARMAP{GenericRead=0x00120089,GenericWrite=0x00120116,GenericExecute=0x001200A0,GenericAll=0x001F01FF};bool ok=AccessCheck(sd,token,0x80,ref map,ps,ref len,out gr,out allowed);return new{callOk=ok,allowed=ok&&allowed,granted=String.Format("0x{0:X8}",gr),error=ok?0:Marshal.GetLastWin32Error()};}
  finally{Marshal.FreeHGlobal(ps);}
 }
 static IntPtr OpenParent(string dir){
  IntPtr h=CreateFileW(dir,0x00100001,7,IntPtr.Zero,3,0x02200000,IntPtr.Zero);
  if(h==new IntPtr(-1))throw new Exception("Open parent directory failed; Win32="+Marshal.GetLastWin32Error());
  return h;
 }
 static object OpenNt(string pathOrName,IntPtr root,uint objFlags,uint createOptions,uint desired){
  string nt = root==IntPtr.Zero ? ("\\??\\"+pathOrName) : pathOrName;
  IntPtr str=IntPtr.Zero,usPtr=IntPtr.Zero,h=IntPtr.Zero;
  try{
   str=Marshal.StringToHGlobalUni(nt);
   RayaARUS us=new RayaARUS{Length=(ushort)(nt.Length*2),MaximumLength=(ushort)(nt.Length*2),Buffer=str};
   usPtr=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(RayaARUS)));Marshal.StructureToPtr(us,usPtr,false);
   RayaAROA oa=new RayaAROA{Length=(uint)Marshal.SizeOf(typeof(RayaAROA)),RootDirectory=root,ObjectName=usPtr,Attributes=objFlags};
   RayaARIOSB iosb;
   int st=NtCreateFile(out h,desired,ref oa,out iosb,IntPtr.Zero,0x80,7,1,createOptions,IntPtr.Zero,0);
   if(st<0)return new{opened=false,status=String.Format("0x{0:X8}",st),grantedAccess=(string)null};
   int n=Marshal.SizeOf(typeof(RayaARBASIC));IntPtr b=Marshal.AllocHGlobal(n);
   try{uint ret;int qs=NtQueryObject(h,0,b,(uint)n,out ret);RayaARBASIC x=qs==0?(RayaARBASIC)Marshal.PtrToStructure(b,typeof(RayaARBASIC)):new RayaARBASIC();return new{opened=true,status=String.Format("0x{0:X8}",st),queryObjectStatus=String.Format("0x{0:X8}",qs),grantedAccess=qs==0?String.Format("0x{0:X8}",x.GrantedAccess):null};}
   finally{Marshal.FreeHGlobal(b);}
  }finally{if(h!=IntPtr.Zero)CloseHandle(h);if(usPtr!=IntPtr.Zero)Marshal.FreeHGlobal(usPtr);if(str!=IntPtr.Zero)Marshal.FreeHGlobal(str);}
 }
 public static object Run(string dir,string file,string sid){
  SetDacl(file,sid);
  IntPtr o,g,d,s,sd;uint ge=GetNamedSecurityInfoW(file,1,7,out o,out g,out d,out s,out sd);if(ge!=0)throw new Exception("GetNamedSecurityInfo="+ge);
  IntPtr primary=IntPtr.Zero,imp=IntPtr.Zero,parent=IntPtr.Zero;
  try{
   if(!OpenProcessToken(GetCurrentProcess(),0x000A,out primary))throw new Exception("OpenProcessToken Win32="+Marshal.GetLastWin32Error());
   if(!DuplicateToken(primary,2,out imp))throw new Exception("DuplicateToken Win32="+Marshal.GetLastWin32Error());
   parent=OpenParent(dir);
   var cases=new[]{
    new{label="prototype-all-flags",obj=0x1040u,opts=0x00200060u},
    new{label="without-OBJ_DONT_REPARSE",obj=0x0040u,opts=0x00200060u},
    new{label="without-OBJ_CASE_INSENSITIVE",obj=0x1000u,opts=0x00200060u},
    new{label="without-object-flags",obj=0x0000u,opts=0x00200060u},
    new{label="without-FILE_OPEN_REPARSE_POINT",obj=0x1040u,opts=0x00000060u},
    new{label="without-FILE_SYNCHRONOUS_IO_NONALERT",obj=0x1040u,opts=0x00200040u}
   };
   var rows=new List<object>();
   foreach(var c in cases){
    rows.Add(new{
     flags=c.label,
     objectAttributes=String.Format("0x{0:X4}",c.obj),
     createOptions=String.Format("0x{0:X8}",c.opts),
     desiredAccess="0x00100080",
     absolute=OpenNt(file,IntPtr.Zero,c.obj,c.opts,0x00100080),
     relativeToHeldParent=OpenNt(System.IO.Path.GetFileName(file),parent,c.obj,c.opts,0x00100080)
    });
   }
   return new{userSid=sid,accessCheckRA=CheckRA(sd,imp),parentHandleOpened=(parent!=IntPtr.Zero),results=rows};
  }finally{if(parent!=IntPtr.Zero)CloseHandle(parent);if(imp!=IntPtr.Zero)CloseHandle(imp);if(primary!=IntPtr.Zero)CloseHandle(primary);LocalFree(sd);}
 }
}
'@
Add-Type -TypeDefinition $source -Language CSharp
$file = Join-Path $fixture 'RayaRelativeTarget.bin'
[System.IO.File]::WriteAllText($file,'ABSOLUTE-RELATIVE-ACL-CANARY',[System.Text.Encoding]::UTF8)
$result = [RayaAbsoluteRelativeAcl]::Run($fixture,$file,$sid)
[pscustomobject]@{
 integrity = (whoami /groups | Select-String 'S-1-16-').ToString().Trim()
 userSid = $sid
 target = 'one disposable file; explicit user DENY FILE_READ_ATTRIBUTES and ALLOW SYNCHRONIZE; SYSTEM full control'
 result = $result
 scope = 'same file and DACL for absolute and RootDirectory-relative NtCreateFile opens; no workspace or persistent ACL changed'
} | ConvertTo-Json -Depth 15
} finally {
 Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
}
