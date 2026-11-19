param()
$ErrorActionPreference='Stop'
$fixture=Join-Path $env:TEMP ('raya-sync-access-query-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force|Out-Null
$file=Join-Path $fixture 'zero-access.txt'
$sid=([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value
try{
 Set-Content -LiteralPath $file -Value 'ZERO-ACCESS-METADATA-CANARY' -NoNewline
 $source=@'
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)]public struct RayaMapZero{public uint GenericRead,GenericWrite,GenericExecute,GenericAll;}
[StructLayout(LayoutKind.Sequential)]public struct RayaBasicZero{public uint Attributes,GrantedAccess,HandleCount,PointerCount,PagedPoolUsage,NonPagedPoolUsage;[MarshalAs(UnmanagedType.ByValArray,SizeConst=3)]public uint[] Reserved;public uint NameInfoSize,TypeInfoSize,SecurityDescriptorSize;public long CreateTime;}
[StructLayout(LayoutKind.Sequential)]public struct RayaUnicodeZero{public ushort Length,MaximumLength;public IntPtr Buffer;}
[StructLayout(LayoutKind.Sequential)]public struct RayaObjectZero{public uint Length;public IntPtr RootDirectory,ObjectName;public uint Attributes;public IntPtr SecurityDescriptor,SecurityQualityOfService;}
[StructLayout(LayoutKind.Sequential)]public struct RayaIoZero{public IntPtr StatusOrPointer;public UIntPtr Information;}
[StructLayout(LayoutKind.Sequential)]public struct RayaFileBasicZero{public long CreationTime,LastAccessTime,LastWriteTime,ChangeTime;public uint FileAttributes;}
[StructLayout(LayoutKind.Sequential)]public struct RayaFileStandardZero{public long AllocationSize,EndOfFile;public uint NumberOfLinks;[MarshalAs(UnmanagedType.U1)]public bool DeletePending;[MarshalAs(UnmanagedType.U1)]public bool Directory;}
public static class RayaSyncAccessQuery{
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool ConvertStringSidToSidW(string s,out IntPtr sid);
 [DllImport("advapi32.dll",SetLastError=true)]static extern bool InitializeAcl(IntPtr acl,uint len,uint rev);
 [DllImport("advapi32.dll",SetLastError=true)]static extern bool AddAccessDeniedAceEx(IntPtr acl,uint rev,uint flags,uint mask,IntPtr sid);
 [DllImport("advapi32.dll",SetLastError=true)]static extern bool AddAccessAllowedAceEx(IntPtr acl,uint rev,uint flags,uint mask,IntPtr sid);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern uint SetNamedSecurityInfoW(string name,int type,uint info,IntPtr owner,IntPtr group,IntPtr dacl,IntPtr sacl);
 [DllImport("advapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern uint GetNamedSecurityInfoW(string n,int t,uint i,out IntPtr o,out IntPtr g,out IntPtr d,out IntPtr s,out IntPtr sd);
 [DllImport("advapi32.dll",SetLastError=true)]static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
 [DllImport("advapi32.dll",SetLastError=true)]static extern bool DuplicateToken(IntPtr t,int l,out IntPtr d);
 [DllImport("advapi32.dll",SetLastError=true)]static extern bool AccessCheck(IntPtr sd,IntPtr token,uint desired,ref RayaMapZero map,IntPtr ps,ref uint pslen,out uint granted,out bool allowed);
 [DllImport("kernel32.dll")]static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern IntPtr CreateFileW(string p,uint a,uint s,IntPtr sa,uint c,uint f,IntPtr t);
 [DllImport("ntdll.dll")]static extern int NtCreateFile(out IntPtr h,uint desired,ref RayaObjectZero oa,out RayaIoZero iosb,IntPtr alloc,uint attrs,uint share,uint disp,uint opts,IntPtr ea,uint eaLen);
 [DllImport("ntdll.dll")]static extern int NtQueryObject(IntPtr h,int cls,IntPtr b,uint n,out uint ret);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetFileInformationByHandleEx(IntPtr h,int cls,IntPtr info,uint size);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetFileSizeEx(IntPtr h,out long size);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool CloseHandle(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)]static extern IntPtr LocalFree(IntPtr p);
 static void Must(bool ok,string label){if(!ok)throw new Exception(label+" "+Marshal.GetLastWin32Error());}
 static void SetDacl(string path,string sid){
  IntPtr u=IntPtr.Zero,sys=IntPtr.Zero,acl=IntPtr.Zero;
  try{Must(ConvertStringSidToSidW(sid,out u),"Convert user SID");Must(ConvertStringSidToSidW("S-1-5-18",out sys),"Convert SYSTEM SID");acl=Marshal.AllocHGlobal(4096);Must(InitializeAcl(acl,4096,2),"InitializeAcl");Must(AddAccessDeniedAceEx(acl,2,0,0x80,u),"Deny RA");Must(AddAccessAllowedAceEx(acl,2,0,0x00100000,u),"Allow SYNCHRONIZE");Must(AddAccessAllowedAceEx(acl,2,0,0x001F01FF,sys),"Allow SYSTEM");uint e=SetNamedSecurityInfoW(path,1,0x80000004,u,IntPtr.Zero,acl,IntPtr.Zero);if(e!=0)throw new Exception("SetNamedSecurityInfo "+e);}
  finally{if(acl!=IntPtr.Zero)Marshal.FreeHGlobal(acl);if(u!=IntPtr.Zero)LocalFree(u);if(sys!=IntPtr.Zero)LocalFree(sys);}
 }
 static object Query(IntPtr h){
  int bn=Marshal.SizeOf(typeof(RayaFileBasicZero)),sn=Marshal.SizeOf(typeof(RayaFileStandardZero));
  IntPtr b=Marshal.AllocHGlobal(bn),s=Marshal.AllocHGlobal(sn);
  try{
   bool bok=GetFileInformationByHandleEx(h,0,b,(uint)bn);int be=bok?0:Marshal.GetLastWin32Error();
   bool sok=GetFileInformationByHandleEx(h,1,s,(uint)sn);int se=sok?0:Marshal.GetLastWin32Error();
   long size=0;bool sizeOk=GetFileSizeEx(h,out size);int sizeErr=sizeOk?0:Marshal.GetLastWin32Error();
   RayaFileBasicZero bi=bok?(RayaFileBasicZero)Marshal.PtrToStructure(b,typeof(RayaFileBasicZero)):new RayaFileBasicZero();
   RayaFileStandardZero si=sok?(RayaFileStandardZero)Marshal.PtrToStructure(s,typeof(RayaFileStandardZero)):new RayaFileStandardZero();
   return new{basicInfoOk=bok,basicInfoError=be,fileAttributes=bok?String.Format("0x{0:X8}",bi.FileAttributes):null,standardInfoOk=sok,standardInfoError=se,endOfFile=sok?si.EndOfFile:(long?)null,numberOfLinks=sok?si.NumberOfLinks:(uint?)null,getFileSizeExOk=sizeOk,getFileSizeExError=sizeErr,size=sizeOk?size:(long?)null};
  }finally{Marshal.FreeHGlobal(b);Marshal.FreeHGlobal(s);}
 }
 static object Actual(IntPtr h){int n=Marshal.SizeOf(typeof(RayaBasicZero));IntPtr b=Marshal.AllocHGlobal(n);try{uint ret;int st=NtQueryObject(h,0,b,(uint)n,out ret);RayaBasicZero x=st==0?(RayaBasicZero)Marshal.PtrToStructure(b,typeof(RayaBasicZero)):new RayaBasicZero();return new{status=String.Format("0x{0:X8}",st),grantedAccess=st==0?String.Format("0x{0:X8}",x.GrantedAccess):null};}finally{Marshal.FreeHGlobal(b);}}
 static object Win32Zero(string path){IntPtr h=CreateFileW(path,0x00100000,7,IntPtr.Zero,3,0x00200000,IntPtr.Zero);if(h==new IntPtr(-1))return new{api="CreateFileW",opened=false,error=Marshal.GetLastWin32Error()};try{return new{api="CreateFileW",opened=true,actual=Actual(h),query=Query(h)};}finally{CloseHandle(h);}}
 static object NtZero(string path){IntPtr name=IntPtr.Zero,usPtr=IntPtr.Zero,h=IntPtr.Zero;try{
  string nt="\\??\\"+path;name=Marshal.StringToHGlobalUni(nt);RayaUnicodeZero us=new RayaUnicodeZero{Length=(ushort)(nt.Length*2),MaximumLength=(ushort)(nt.Length*2),Buffer=name};usPtr=Marshal.AllocHGlobal(Marshal.SizeOf(typeof(RayaUnicodeZero)));Marshal.StructureToPtr(us,usPtr,false);
  RayaObjectZero oa=new RayaObjectZero{Length=(uint)Marshal.SizeOf(typeof(RayaObjectZero)),RootDirectory=IntPtr.Zero,ObjectName=usPtr,Attributes=0x40,SecurityDescriptor=IntPtr.Zero,SecurityQualityOfService=IntPtr.Zero};RayaIoZero iosb;
  int st=NtCreateFile(out h,0x00100000,ref oa,out iosb,IntPtr.Zero,0x80,7,1,0x00200040,IntPtr.Zero,0);
  if(st<0)return new{api="NtCreateFile",opened=false,ntstatus=String.Format("0x{0:X8}",st)};
  try{return new{api="NtCreateFile",opened=true,ntstatus=String.Format("0x{0:X8}",st),actual=Actual(h),query=Query(h)};}finally{CloseHandle(h);h=IntPtr.Zero;}
 }finally{if(h!=IntPtr.Zero)CloseHandle(h);if(usPtr!=IntPtr.Zero)Marshal.FreeHGlobal(usPtr);if(name!=IntPtr.Zero)Marshal.FreeHGlobal(name);}}
 public static object Run(string path,string sid){
  SetDacl(path,sid);IntPtr o,g,d,s,sd;uint ge=GetNamedSecurityInfoW(path,1,7,out o,out g,out d,out s,out sd);if(ge!=0)throw new Exception("GetNamedSecurityInfo "+ge);
  IntPtr primary=IntPtr.Zero,imp=IntPtr.Zero;
  try{if(!OpenProcessToken(GetCurrentProcess(),0x000A,out primary))throw new Exception("OpenProcessToken "+Marshal.GetLastWin32Error());if(!DuplicateToken(primary,2,out imp))throw new Exception("DuplicateToken "+Marshal.GetLastWin32Error());
   IntPtr ps=Marshal.AllocHGlobal(1024);Marshal.Copy(new byte[1024],0,ps,1024);object ac;
   try{uint len=1024,gr=0;bool al=false;RayaMapZero m=new RayaMapZero{GenericRead=0x00120089,GenericWrite=0x00120116,GenericExecute=0x001200A0,GenericAll=0x001F01FF};bool ok=AccessCheck(sd,imp,0x80,ref m,ps,ref len,out gr,out al);ac=new{callOk=ok,allowed=ok&&al,granted=String.Format("0x{0:X8}",gr),error=ok?0:Marshal.GetLastWin32Error()};}finally{Marshal.FreeHGlobal(ps);}
   return new{pathAccessCheckRA=ac,win32SyncAccess=Win32Zero(path),ntSyncAccess=NtZero(path)};
  }finally{if(imp!=IntPtr.Zero)CloseHandle(imp);if(primary!=IntPtr.Zero)CloseHandle(primary);LocalFree(sd);}
 }
}
'@
 Add-Type -TypeDefinition $source -Language CSharp
 $probe=[RayaSyncAccessQuery]::Run($file,$sid)
 $acl=(& icacls.exe $file)-join [Environment]::NewLine
 [pscustomobject]@{userSid=$sid;integrity=(whoami /groups|Select-String 'S-1-16-').ToString().Trim();acl=$acl;probe=$probe;scope='one disposable temp NTFS file; explicit RA deny and SYNCHRONIZE allow; SYNCHRONIZE-only Win32 and native opens; fixture deleted'}|ConvertTo-Json -Depth 12
}finally{Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue}