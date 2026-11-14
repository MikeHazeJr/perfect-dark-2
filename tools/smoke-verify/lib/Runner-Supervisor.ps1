# One persistent native accounting owner; public launch requires immutable
# exact-source/host native qualification, published after independent review.
$loadedSources=Get-Variable -Name SmokeReceiptLoadedSources -Scope Script -ErrorAction SilentlyContinue
if($null -eq $loadedSources){$script:SmokeReceiptLoadedSources=@{}}
$loadedPath=[IO.Path]::GetFullPath($PSCommandPath)
$loadedHash=(Get-FileHash -LiteralPath $loadedPath -Algorithm SHA256).Hash.ToLowerInvariant()
if($script:SmokeReceiptLoadedSources.ContainsKey($loadedPath) -and $script:SmokeReceiptLoadedSources[$loadedPath] -cne $loadedHash){throw 'Receipt supervisor module changed in a live interpreter; preserve its original operations.'}
$script:SmokeReceiptLoadedSources[$loadedPath]=$loadedHash
function Initialize-SmokeRunnerSupervisorType {
    $source=@'
using System;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading.Tasks;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading;
using Microsoft.Win32.SafeHandles;
public sealed class PdReceiptSupervisor {
    public const string SourceSha256="PD_RECEIPT_SUPERVISOR_SOURCE";
    [StructLayout(LayoutKind.Sequential)] struct Security { public int Size; public IntPtr Descriptor; public int Inherit; }
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct Startup {
        public int Size;public string Reserved,Desktop,Title;public uint X,Y,XSize,YSize,XChars,YChars,Fill,Flags;
        public ushort Show,ReservedSize;public IntPtr ReservedBytes,Input,Output,Error;
    }
    [StructLayout(LayoutKind.Sequential)] struct StartupEx { public Startup Info;public IntPtr Attributes; }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr Process,Thread;public uint Id,ThreadId; }
    [StructLayout(LayoutKind.Sequential)] struct Accounting { public long User,Kernel,PeriodUser,PeriodKernel;public uint Faults,Total,Active,Terminated; }
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateJobObjectW(ref Security security,string name);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job,int type,out Accounting data,int size,IntPtr length);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool CreatePipe(out IntPtr read,out IntPtr write,ref Security security,uint size);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetHandleInformation(IntPtr handle,uint mask,uint flags);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string path,uint access,uint share,ref Security attributes,uint disposition,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool InitializeProcThreadAttributeList(IntPtr list,int count,uint flags,ref IntPtr size);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool UpdateProcThreadAttribute(IntPtr list,uint flags,IntPtr key,IntPtr value,IntPtr size,IntPtr previous,IntPtr returned);
    [DllImport("kernel32.dll")] static extern void DeleteProcThreadAttributeList(IntPtr list);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateProcessW(string app,StringBuilder arguments,IntPtr processAttributes,IntPtr threadAttributes,bool inherit,uint flags,IntPtr environment,string cwd,ref StartupEx startup,out ProcessInfo process);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll",SetLastError=true)] static extern uint GetProcessId(IntPtr process);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern uint GetCurrentProcessId();
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr process,out long created,out long exited,out long kernel,out long user);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool IsProcessInJob(IntPtr process,IntPtr job,out bool inside);
    [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr handle,out uint code);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetNamedPipeServerProcessId(IntPtr pipe,out uint pid);
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageNameW(IntPtr process,uint flags,StringBuilder path,ref uint length);
    IntPtr job=IntPtr.Zero,root=IntPtr.Zero,inputRead=IntPtr.Zero,inputWrite=IntPtr.Zero,nul=IntPtr.Zero,thread=IntPtr.Zero;
    IntPtr observedRoot=IntPtr.Zero,receivedRoot=IntPtr.Zero,receivedJob=IntPtr.Zero;
    bool exitVerified,serverValidated,witnessesValidated;int verifiedExit;
    bool creationAttempted,creationReturned,creationSucceeded,exclusiveJob;
    Task<FailedSetupCloseout> failedSetupWork;
    string originalSetupStage,originalSetupFailure;int originalSetupError;
    long attachmentDeadline;string attachmentQueuePath,attachmentQueueHash;
    int observedPid;
    public interface IIdentityProbe {
        long Created(long handle,int pid);
        string Image(long handle);
        bool InJob(long process,long domain);
    }
    readonly IIdentityProbe identityProbe;
    public int RootPid {get;private set;}
    public long CreationTicks {get;private set;}
    public uint CreationFlags {get;private set;}
    public bool NativeCreated {get {return creationSucceeded;}}
    public string Operation {get;private set;}
    public string Executable {get;private set;}
    public int ControllerPid {get;private set;}
    public long ControllerCreationTicks {get;private set;}
    public string Stage {get;private set;}
    public int NativeError {get;private set;}
    public string Failure {get;private set;}
    public Task StartupWork {get;private set;}
    public Closeout Proof {get;private set;}
    public sealed class FailedSetupCloseout {
        public bool NoChildCreated {get {return true;}}
        public bool OwnHandlesClosed {get {return true;}}
        public bool ForeignJobNotControlled {get;private set;}
        public string Operation {get;private set;}
        public string FailureStage {get;private set;}
        public string Failure {get;private set;}
        public int NativeError {get;private set;}
        public string SourceHash {get {return SourceSha256;}}
        internal FailedSetupCloseout(PdReceiptSupervisor owner,string stage,string failure,int nativeError){
            Operation=owner.Operation;ForeignJobNotControlled=!owner.exclusiveJob;
            FailureStage=stage;Failure=failure;NativeError=nativeError;
        }
    }
    public sealed class Closeout {
        public string Operation {get;private set;}
        public int RootPid {get;private set;}
        public long CreationTicks {get;private set;}
        public uint DomainActive {get {return 0;}}
        public bool RootExited {get {return true;}}
        public bool HandlesClosed {get {return true;}}
        public string OutputMode {get {return "nul";}}
        public int ExitCode {get;private set;}
        public string SourceHash {get {return SourceSha256;}}
        internal Closeout(PdReceiptSupervisor owner,int code){Operation=owner.Operation;RootPid=owner.RootPid;CreationTicks=owner.CreationTicks;ExitCode=code;}
    }
    static void Require(bool value,string message){if(!value)throw new Win32Exception(Marshal.GetLastWin32Error(),message);}
    public static IntPtr CheckedOwnedClose(IntPtr value,Func<IntPtr,bool> close,Func<int> error){
        if(value==IntPtr.Zero)return value;
        if(!close(value))throw new Win32Exception(error(),"Close exact supervisor witness");
        return IntPtr.Zero;
    }
    static void Close(ref IntPtr value){value=CheckedOwnedClose(value,CloseHandle,Marshal.GetLastWin32Error);}
    static long Created(IntPtr process,int pid){
        if(GetProcessId(process)!=(uint)pid)throw new IOException("Supervisor handle PID mismatch");
        long created,exited,kernel,user;Require(GetProcessTimes(process,out created,out exited,out kernel,out user),"Read held supervisor identity");return created;
    }
    void Record(Exception error){Failure=error.Message.Substring(0,Math.Min(2048,error.Message.Length));var native=error as Win32Exception;NativeError=native==null?0:native.NativeErrorCode;}
    // Constructor creates no native resources; source tests may compile types.
    public PdReceiptSupervisor(string operation,string executable){Operation=operation;Executable=executable;ControllerPid=(int)GetCurrentProcessId();ControllerCreationTicks=Created(GetCurrentProcess(),ControllerPid);}
    public void SetAttachmentAdmission(long deadline,string queuePath,string queueHash){
        if(identityProbe!=null)throw new IOException("Inert identity adapter cannot attach");
        RequireCreationDeadline(DateTime.UtcNow.Ticks,deadline);
        attachmentDeadline=deadline;attachmentQueuePath=queuePath;attachmentQueueHash=queueHash;
    }
    // Explicit inert identity seam for source checks. It cannot launch, connect,
    // close native handles or manufacture a native closeout proof.
    public PdReceiptSupervisor(string operation,string executable,IIdentityProbe probe,int controller,long creation){
        if(probe==null)throw new ArgumentNullException("probe");identityProbe=probe;
        Operation=operation;Executable=executable;ControllerPid=controller;ControllerCreationTicks=creation;
    }
    long ReadCreated(IntPtr handle,int pid){return identityProbe==null?Created(handle,pid):identityProbe.Created(handle.ToInt64(),pid);}
    string ReadImage(IntPtr handle){
        if(identityProbe!=null)return identityProbe.Image(handle.ToInt64());
        var image=new StringBuilder(32768);uint length=(uint)image.Capacity;
        Require(QueryFullProcessImageNameW(handle,0,image,ref length),"Read broker interpreter");return image.ToString();
    }
    bool ReadMembership(IntPtr process,IntPtr domain){
        if(identityProbe!=null)return identityProbe.InJob(process.ToInt64(),domain.ToInt64());
        bool inside;Require(IsProcessInJob(process,domain,out inside),"Read transferred supervisor domain");return inside;
    }
    public void ValidateHeldServer(long process,int server){
        if(observedRoot==IntPtr.Zero){observedRoot=new IntPtr(process);observedPid=server;}
        if(observedRoot.ToInt64()!=process||observedPid!=server)throw new IOException("Broker server changed while raw validation is retained");
        if(!serverValidated){
            if(!String.Equals(ReadImage(observedRoot),Executable,StringComparison.OrdinalIgnoreCase))throw new IOException("Broker interpreter mismatch");
            if(RootPid!=0&&RootPid!=server)throw new IOException("Broker root differs from owned launch");
            long created=ReadCreated(observedRoot,server);if(CreationTicks!=0&&CreationTicks!=created)throw new IOException("Broker creation differs from owned launch");
            RootPid=server;CreationTicks=created;serverValidated=true;
        }else if(server!=RootPid||ReadCreated(observedRoot,RootPid)!=CreationTicks)throw new IOException("Broker generation changed");
    }
    public static void RequireCreationDeadline(long now,long deadline){
        if(deadline<=0||now>=deadline)throw new IOException("Supervisor creation admission expired");
    }
    public static void WithCoordinationAdmission(Func<bool> wait,Action release,Action boundary){
        bool taken=false;
        try{
            try{taken=wait();}
            catch(AbandonedMutexException){taken=true;throw new IOException("Supervisor coordination mutex abandoned; admission refused");}
            if(!taken)throw new IOException("Supervisor admission FIFO mutex unavailable");
            boundary();
        }finally{if(taken)release();}
    }
    public static void RequireQueueHash(string path,string expected){
        using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)){
            if(stream.Length>4194304)throw new IOException("Supervisor FIFO document exceeds bound");
            string actual;using(var hash=SHA256.Create()){actual=BitConverter.ToString(hash.ComputeHash(stream)).Replace("-","").ToLowerInvariant();}
            if(!String.Equals(actual,expected,StringComparison.Ordinal))throw new IOException("Supervisor FIFO changed before native boundary");
        }
    }
    void AttachmentBoundary(Action boundary){
        if(attachmentDeadline==0){boundary();return;}
        using(var coordination=new Mutex(false,"PerfectDark2CodexCoordination")){
            WithCoordinationAdmission(()=>coordination.WaitOne(500),()=>coordination.ReleaseMutex(),()=>{
                RequireQueueHash(attachmentQueuePath,attachmentQueueHash);
                RequireCreationDeadline(DateTime.UtcNow.Ticks,attachmentDeadline);
                boundary();
            });
        }
    }
    string QualifiedMessage(string message){
        if(attachmentDeadline==0)return message;
        if(!message.EndsWith("}",StringComparison.Ordinal)||attachmentQueuePath.IndexOf('"')>=0)throw new IOException("Invalid qualified IPC envelope");
        return message.Substring(0,message.Length-1)+",\"admission\":{\"deadline_utc_ticks\":"+attachmentDeadline+
            ",\"queue_path\":\""+attachmentQueuePath.Replace("\\","/")+"\",\"queue_sha256\":\""+attachmentQueueHash+"\"}}";
    }
    public static byte[] TransferWithAdmission(Action<Action> guard,Action request,Func<byte[]> receive,Action<byte[]> ack){
        guard(request);
        byte[] reply=receive(); // No shared coordination mutex across a read.
        guard(()=>ack(reply));
        return reply;
    }
    public Task BeginStart(string helper,string specBase64,byte[] request,string cwd,long deadline,string queuePath,string queueSha256){
        if(identityProbe!=null)throw new IOException("Inert identity adapter cannot launch");
        if(StartupWork!=null)return StartupWork;
        if(request.Length>33554432||specBase64.Length>12288)throw new IOException("Supervisor request exceeds finite bound");
        RequireCreationDeadline(DateTime.UtcNow.Ticks,deadline);
        StartupWork=Task.Run(()=>Start(helper,specBase64,request,cwd,deadline,queuePath,queueSha256));return StartupWork;
    }
    void Start(string helper,string spec,byte[] request,string cwd,long deadline,string queuePath,string queueSha256){
        IntPtr attributes=IntPtr.Zero,handles=IntPtr.Zero,jobs=IntPtr.Zero;bool ready=false;
        try{
            var security=new Security{Size=Marshal.SizeOf(typeof(Security)),Inherit=1};
            Stage="reserve named supervisor domain";
            job=CreateJobObjectW(ref security,"Local\\PD2Receipt-"+Operation);Require(job!=IntPtr.Zero,"Create supervisor job");
            if(Marshal.GetLastWin32Error()==183)throw new Win32Exception(183,"Supervisor operation already has a native owner; never redispatch");
            exclusiveJob=true;
            Stage="prepare bounded input and NUL outputs";
            Require(CreatePipe(out inputRead,out inputWrite,ref security,0),"Create supervisor input");
            Require(SetHandleInformation(inputWrite,1,0),"Exclude input writer inheritance");
            nul=CreateFileW("NUL",0x40000000,3,ref security,3,0,IntPtr.Zero);
            if(nul==new IntPtr(-1)){nul=IntPtr.Zero;Require(false,"Open supervisor NUL output");}
            Stage="prepare native creation attributes";IntPtr bytes=IntPtr.Zero;
            InitializeProcThreadAttributeList(IntPtr.Zero,2,0,ref bytes);if(bytes==IntPtr.Zero)throw new IOException("Missing supervisor attribute size");
            attributes=Marshal.AllocHGlobal(bytes);Require(InitializeProcThreadAttributeList(attributes,2,0,ref bytes),"Initialize supervisor attributes");ready=true;
            handles=Marshal.AllocHGlobal(IntPtr.Size*3);Marshal.WriteIntPtr(handles,0,inputRead);Marshal.WriteIntPtr(handles,IntPtr.Size,nul);Marshal.WriteIntPtr(handles,IntPtr.Size*2,job);
            jobs=Marshal.AllocHGlobal(IntPtr.Size);Marshal.WriteIntPtr(jobs,job);
            Require(UpdateProcThreadAttribute(attributes,0,new IntPtr(0x20002),handles,new IntPtr(IntPtr.Size*3),IntPtr.Zero,IntPtr.Zero),"Bind exact supervisor inheritance");
            Require(UpdateProcThreadAttribute(attributes,0,new IntPtr(0x2000D),jobs,new IntPtr(IntPtr.Size),IntPtr.Zero,IntPtr.Zero),"Enroll supervisor at creation");
            var startup=new StartupEx();startup.Info.Size=Marshal.SizeOf(typeof(StartupEx));startup.Info.Flags=0x100;startup.Info.Input=inputRead;startup.Info.Output=nul;startup.Info.Error=nul;startup.Attributes=attributes;
            string command="\""+Executable+"\" -X utf8 -B \""+helper+"\" --spec "+spec+" --job "+job.ToInt64();
            if(command.Length>30000)throw new IOException("Supervisor command exceeds native bound");
            Stage="create contained supervisor";ProcessInfo process=new ProcessInfo();
            using(var coordination=new Mutex(false,"PerfectDark2CodexCoordination")){
                Action create=()=>{
                    RequireCreationDeadline(DateTime.UtcNow.Ticks,deadline);
                    creationAttempted=true;
                    CreationFlags=0x08080000;
                    bool created=CreateProcessW(Executable,new StringBuilder(command),IntPtr.Zero,IntPtr.Zero,true,CreationFlags,IntPtr.Zero,cwd,ref startup,out process);
                    creationReturned=true;creationSucceeded=created;
                    if(created){root=process.Process;thread=process.Thread;RootPid=(int)process.Id;}
                    Require(created,"Create receipt supervisor");
                };
                if(String.IsNullOrEmpty(queuePath)){create();}
                else{WithCoordinationAdmission(()=>coordination.WaitOne(500),()=>coordination.ReleaseMutex(),()=>{RequireQueueHash(queuePath,queueSha256);create();});}
            }
            CreationTicks=Created(root,RootPid);
            Close(ref inputRead);Close(ref nul);Close(ref thread);
            Stage="transfer immutable request";
            using(var writer=new FileStream(new SafeFileHandle(inputWrite,false),FileAccess.Write,4096,false)){writer.Write(request,0,request.Length);writer.Flush();}
            Close(ref inputWrite);Stage="broker owns receipt";
        }catch(Exception error){originalSetupStage=Stage;Record(error);originalSetupFailure=Failure;originalSetupError=NativeError;throw;}
        finally{if(ready)DeleteProcThreadAttributeList(attributes);foreach(IntPtr value in new[]{attributes,handles,jobs})if(value!=IntPtr.Zero)Marshal.FreeHGlobal(value);}
    }
    public Task<string> Exchange(string message){if(identityProbe!=null)throw new IOException("Inert identity adapter cannot connect");return Task.Run(()=>ExchangeCore(message));}
    string ExchangeCore(string message){
        try{
            Stage="bounded local supervisor IPC";
            byte[] payload=Encoding.UTF8.GetBytes(QualifiedMessage(message));if(payload.Length>16384)throw new IOException("Supervisor IPC frame exceeds bound");
            using(var pipe=new NamedPipeClientStream(".","PD2Receipt-"+Operation,PipeDirection.InOut,PipeOptions.None)){
                pipe.Connect(500);pipe.ReadMode=PipeTransmissionMode.Message;
                uint server;Require(GetNamedPipeServerProcessId(pipe.SafePipeHandle.DangerousGetHandle(),out server),"Read native supervisor server PID");
                if(observedRoot==IntPtr.Zero){
                    observedRoot=OpenProcess(0x1000|0x100000,false,server);Require(observedRoot!=IntPtr.Zero,"Hold exact broker server");
                    observedPid=(int)server;
                }
                ValidateHeldServer(observedRoot.ToInt64(),(int)server);
                byte[] response=TransferWithAdmission(AttachmentBoundary,
                    ()=>{pipe.Write(payload,0,payload.Length);pipe.Flush();},
                    ()=>{
                        byte[] buffer=new byte[16384];int count=pipe.Read(buffer,0,buffer.Length);
                        if(count<=0||!pipe.IsMessageComplete)throw new IOException("Supervisor reply incomplete/oversized");
                        byte[] exact=new byte[count];Buffer.BlockCopy(buffer,0,exact,0,count);return exact;
                    },
                    reply=>{
                        string digest;using(var hash=SHA256.Create()){digest=BitConverter.ToString(hash.ComputeHash(reply)).Replace("-","").ToLowerInvariant();}
                        byte[] ack=Encoding.UTF8.GetBytes("{\"ack_sha256\":\""+digest+"\"}");
                        pipe.Write(ack,0,ack.Length);pipe.Flush();
                    });
                return Encoding.UTF8.GetString(response);
            }
        }catch(Exception error){Record(error);throw;}
    }
    public void AcceptWitnesses(long process,long domain,int pid,long creation){
        if(pid!=RootPid||creation!=CreationTicks)throw new IOException("Broker reply identity mismatch");
        if(receivedRoot!=IntPtr.Zero||receivedJob!=IntPtr.Zero){
            if(receivedRoot.ToInt64()!=process||receivedJob.ToInt64()!=domain)throw new IOException("Broker replaced retained witnesses");
        }else{
            // Raw ownership and successful validation are separate states.
            receivedRoot=new IntPtr(process);receivedJob=new IntPtr(domain);
        }
        if(witnessesValidated)return;
        if(!serverValidated||receivedRoot==IntPtr.Zero||receivedJob==IntPtr.Zero||ReadCreated(receivedRoot,pid)!=creation)throw new IOException("Transferred root identity mismatch");
        if(!ReadMembership(receivedRoot,receivedJob))throw new IOException("Transferred supervisor domain mismatch");
        witnessesValidated=true;
    }
    public Task<Closeout> BeginCloseout(){if(identityProbe!=null)throw new IOException("Inert identity adapter cannot close native resources");return Task.Run(()=>CloseoutCore());}
    public Task<FailedSetupCloseout> BeginFailedSetupCloseout(){
        if(identityProbe!=null)throw new IOException("Inert identity adapter cannot close native resources");
        if(!CanCloseFailedSetup(StartupWork!=null&&StartupWork.IsCompleted&&StartupWork.IsFaulted,
            creationSucceeded,root!=IntPtr.Zero,thread!=IntPtr.Zero,RootPid,creationAttempted,creationReturned))
            throw new IOException("Failed setup has no authoritative no-child outcome; retain original witnesses");
        if(failedSetupWork!=null&&!failedSetupWork.IsFaulted)return failedSetupWork;
        string stage=originalSetupStage,failure=originalSetupFailure;int nativeError=originalSetupError;
        failedSetupWork=Task.Run(()=>{
            // These are this attempted launch's exact handles. A collided job
            // is never queried, claimed empty or controlled; close our handle.
            try{
                Stage="close failed setup own handles";
                Close(ref inputRead);Close(ref inputWrite);Close(ref nul);Close(ref thread);
                Close(ref job);Close(ref observedRoot);Close(ref receivedRoot);Close(ref receivedJob);
                var proof=new FailedSetupCloseout(this,stage,failure,nativeError);
                Stage="failed setup own handles closed";return proof;
            }catch(Exception error){Record(error);throw;}
        });
        return failedSetupWork;
    }
    public static bool CanCloseFailedSetup(bool finishedFaulted,bool created,bool rootHeld,bool threadHeld,int pid,bool attempted,bool returned){
        return finishedFaulted&&!created&&!rootHeld&&!threadHeld&&pid==0&&(!attempted||returned);
    }
    Closeout CloseoutCore(){
        try{
            if(Proof!=null)return Proof;
            if(!exitVerified){
                if(!serverValidated||!witnessesValidated||receivedRoot==IntPtr.Zero||receivedJob==IntPtr.Zero)throw new IOException("Missing validated supervisor witnesses");
                uint wait=WaitForSingleObject(receivedRoot,0);if(wait==0xFFFFFFFF)Require(false,"Wait supervisor root");
                if(wait!=0)return null;
                Accounting accounting;Require(QueryInformationJobObject(receivedJob,1,out accounting,Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero),"Query held supervisor domain");
                if(accounting.Active!=0)return null;
                uint code;Require(GetExitCodeProcess(receivedRoot,out code),"Read exact supervisor exit");
                verifiedExit=unchecked((int)code);exitVerified=true;
            }
            Stage="close exact supervisor handles";
            Close(ref inputRead);Close(ref inputWrite);Close(ref nul);Close(ref thread);Close(ref root);Close(ref job);Close(ref observedRoot);Close(ref receivedRoot);Close(ref receivedJob);
            Proof=new Closeout(this,verifiedExit);Stage="physically closed";return Proof;
        }catch(Exception error){Record(error);throw;}
    }
    // No finalizer, kills, global scans, breakaway or KILL_ON_JOB_CLOSE.
}
'@
    $hash=[Security.Cryptography.SHA256]::Create()
    try{$digest=[BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($source))).Replace('-','').ToLowerInvariant()}finally{$hash.Dispose()}
    if('PdReceiptSupervisor' -as [type]){
        if([PdReceiptSupervisor]::SourceSha256 -cne $digest){throw 'Loaded receipt supervisor source differs; no launch admitted.'}
        return
    }
    Add-Type -TypeDefinition ($source.Replace('PD_RECEIPT_SUPERVISOR_SOURCE',$digest))
}

function Get-SmokeRunnerSupervisorSources {
    $root=Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
    $sources=@{}
    foreach($relative in @('tools/smoke-verify/runner_supervisor.py','tools/smoke-verify/runner_retention.py','tools/smoke-verify/storage.py','devtools/smoke-storage-manifest.py','tools/smoke-verify/lib/Storage-Harness.ps1','tools/smoke-verify/lib/Runner-Supervisor.ps1','tools/smoke-verify/lib/Runner-Retention.ps1','devtools/Build-Retention.ps1','devtools/build-session.ps1')){
        $path=Join-Path $root $relative;Assert-SmokePlainTree -Path $path
        $sources[$relative]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if($script:SmokeReceiptLoadedSources.ContainsKey($path) -and $script:SmokeReceiptLoadedSources[$path] -cne $sources[$relative]){throw 'Receipt supervisor loaded source differs from current source; no cached launch admitted.'}
    }
    $text=(@($sources.Keys|Sort-Object|ForEach-Object {$_+'='+$sources[$_]+"`n"}) -join '')
    $hash=[Security.Cryptography.SHA256]::Create()
    try{$bundle=[BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-','').ToLowerInvariant()}finally{$hash.Dispose()}
    return [pscustomobject]@{Root=$root;Sources=$sources;Sha256=$bundle}
}

function New-SmokeRunnerSupervisorDescriptor($Context,[string]$Name,$Value,[switch]$Qualification){
    if($Context.Owner.pid -ne $PID){throw 'Supervisor receipt requires original live context owner.'}
    if($Context.RunId -cnotmatch '^runner-[0-9a-f]{32}$' -or $Name -cnotmatch '^parent-closeout-[0-9a-f]{32}(-completion)?\.json$'){throw 'Invalid supervisor parent/name.'}
    $closure=Get-SmokeRunnerSupervisorSources;$sources=$closure.Sources;$root=$closure.Root
    $raw=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $Value -Depth 50 -Compress))
    if($raw.Length -gt 33554432){throw 'Supervisor request exceeds finite byte bound.'}
    $hash=[Security.Cryptography.SHA256]::Create()
    try{
        $bodyHash=[BitConverter]::ToString($hash.ComputeHash($raw)).Replace('-','').ToLowerInvariant()
        $operation=[BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($Context.RunId+"`n"+$Name))).Replace('-','').ToLowerInvariant().Substring(0,32)
    }finally{$hash.Dispose()}
    $spec=@{schema='pd2.receipt-supervisor.v1';qualification_required=[bool]$Qualification;operation=$operation;project=$Context.ProjectRoot;policy=$script:SmokeStoragePolicy;
        parent_run=$Context.RunId;parent_owner=$Context.Owner;name=$Name;request_sha256=$bodyHash;sources=$sources}
    return [pscustomobject]@{Spec=$spec;Raw=$raw;SourceRoot=$root;ReceiptRoot=(Join-Path $Context.ProjectRoot ('.claude/smoke-storage/receipts/runner-'+$operation))}
}

function Get-SmokeSupervisorDigest([byte[]]$Bytes){
    $hash=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($hash.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant()}finally{$hash.Dispose()}
}
function Assert-SmokeSupervisorHostFile([string]$Path) {
    # Serviced Windows binaries may be hard linked. They have no redirecting
    # reparse point; fingerprint the bytes at the selected actual host path.
    # This rule applies only to host files, never fixtures or proof artifacts.
    $cursor=[IO.Path]::GetFullPath($Path)
    $file=Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
    if($file.PSIsContainer){throw 'Supervisor host identity requires a file.'}
    while($cursor){
        $item=Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
        if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            ($item.PSObject.Properties.Match('LinkType').Count -gt 0 -and
                $item.LinkType -and $item.LinkType -cne 'HardLink')){
            throw ('Supervisor host redirect rejected: '+$cursor)
        }
        $next=Split-Path -Parent $cursor
        if($next -eq $cursor){break};$cursor=$next
    }
}
function Get-SmokeSupervisorHost {
    # Read binary identities only. Do not run a Python probe before admission.
    $process=[Diagnostics.Process]::GetCurrentProcess()
    try{$executable=$process.MainModule.FileName}finally{$process.Dispose()}
    $python=[IO.Path]::GetFullPath('C:\Python312\python.exe')
    $binaries=[ordered]@{}
    foreach($path in @($executable,$python,'C:\Python312\python312.dll','C:\Python312\DLLs\_ctypes.pyd',
            'C:\Python312\Lib\ctypes\__init__.py','C:\Python312\Lib\ctypes\wintypes.py',
            [Management.Automation.PSObject].Assembly.Location,[string].Assembly.Location)){
        Assert-SmokeSupervisorHostFile $path
        $binaries[$path]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $hostRecord=[ordered]@{machine=[Environment]::MachineName;os=[Environment]::OSVersion.Version.ToString();
        architecture=if([Environment]::Is64BitProcess){'x64'}else{'x86'};
        powershell_path=$executable;powershell_version=$PSVersionTable.PSVersion.ToString();
        powershell_edition=$PSVersionTable.PSEdition;clr=[Environment]::Version.ToString();
        python_path=$python;python_version=[Diagnostics.FileVersionInfo]::GetVersionInfo($python).FileVersion;binaries=$binaries}
    $raw=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json $hostRecord -Depth 10 -Compress))
    return [pscustomobject]@{Value=$hostRecord;Key=(Get-SmokeSupervisorDigest $raw)}
}
function Assert-SmokeSupervisorFields($Value,[string[]]$Fields){
    if($null -eq $Value -or $Value -is [string] -or $Value -is [array]){throw 'Supervisor contract requires an object.'}
    $actual=@($Value.PSObject.Properties|ForEach-Object Name|Sort-Object)
    if(($actual -join ',') -cne (($Fields|Sort-Object) -join ',')){throw 'Supervisor contract field set differs.'}
}
function Assert-SmokeSupervisorInteger($Value,[long]$Minimum=1,[long]$Maximum=[long]::MaxValue){
    if(($Value -isnot [int] -and $Value -isnot [long]) -or $Value -lt $Minimum -or $Value -gt $Maximum){throw 'Supervisor integral field outside exact typed bounds.'}
}
function Read-SmokeSupervisorUtc($Value){
    if($Value -isnot [string] -or $Value -cnotmatch '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]{1,7})?Z$'){throw 'Supervisor timestamp requires an exact UTC string.'}
    return [DateTimeOffset]::ParseExact($Value,[string[]]@('yyyy-MM-ddTHH:mm:ssZ','yyyy-MM-ddTHH:mm:ss.FFFFFFFZ'),
        [Globalization.CultureInfo]::InvariantCulture,([Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal))
}
function Assert-SmokeSupervisorMemory($Value,[double]$Minimum){
    if(($Value -isnot [int] -and $Value -isnot [long] -and $Value -isnot [double] -and $Value -isnot [decimal]) -or
        [double]::IsNaN([double]$Value) -or [double]::IsInfinity([double]$Value) -or $Value -lt $Minimum -or $Value -gt 1073741824){throw 'Supervisor memory requires bounded finite numeric admission.'}
}
function Get-SmokeSupervisorArtifactPath($Reference,[string]$Root){
    Assert-SmokeSupervisorFields $Reference @('path','sha256')
    if($Reference.path -isnot [string] -or $Reference.path -cnotmatch '^\.claude/[A-Za-z0-9._/-]+$' -or
        $Reference.path -match '(^|/)\.\.?(/|$)' -or $Reference.sha256 -isnot [string] -or
        $Reference.sha256 -cnotmatch '^[0-9a-f]{64}$'){throw 'Supervisor artifact path/hash contract differs.'}
    $path=[IO.Path]::GetFullPath((Join-Path $Root $Reference.path))
    if(-not $path.StartsWith($Root+'\.claude\',[StringComparison]::Ordinal)){throw 'Supervisor artifact escaped canonical workspace.'}
    return $path
}
function Read-SmokeSupervisorJson([string]$Path,[int]$Limit=65536,[string]$ExpectedHash='',[switch]$OperatorDocument){
    Assert-SmokePlainTree -Path $Path
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
        if($stream.Length -lt 2 -or $stream.Length -gt $Limit){throw 'Supervisor JSON outside bounded byte contract.'}
        $bytes=New-Object byte[] ([int]$stream.Length)
        $offset=0
        while($offset -lt $bytes.Length){$count=$stream.Read($bytes,$offset,$bytes.Length-$offset);if($count -le 0){throw 'Incomplete supervisor JSON read.'};$offset+=$count}
        $digest=Get-SmokeSupervisorDigest $bytes
        if($ExpectedHash -and ($ExpectedHash -cnotmatch '^[0-9a-f]{64}$' -or $digest -cne $ExpectedHash)){throw 'Supervisor immutable artifact hash mismatch.'}
        $encoding=New-Object Text.UTF8Encoding($false,$true)
        $text=$encoding.GetString($bytes)
        # PS7.5+ otherwise promotes ISO text to DateTime. Preserve the strict
        # record's timestamp type; PS5.1 already preserves ISO strings.
        if((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')){$value=ConvertFrom-Json $text -DateKind String}
        else{$value=ConvertFrom-Json $text}
        # Producer uses compact JSON in preserved field order. Round-trip exact
        # bytes reject duplicate/case-alias keys, extra whitespace and lossy data
        # on both PS5.1 and PS7, without source normalization or a permissive parser.
        if(-not $OperatorDocument -and (ConvertTo-Json $value -Depth 50 -Compress) -cne $text){throw 'Supervisor JSON is not canonical lossless data.'}
        return [pscustomobject]@{Value=$value;Sha256=$digest;Bytes=$bytes.Length;Path=$Path}
    }finally{$stream.Dispose()}
}
function Read-SmokeSupervisorArtifact($Reference,[string]$Root){
    $path=Get-SmokeSupervisorArtifactPath $Reference $Root
    return Read-SmokeSupervisorJson $path 65536 $Reference.sha256
}
function Read-SmokeSupervisorReport($Reference,[string]$Root){
    $path=Get-SmokeSupervisorArtifactPath $Reference $Root
    Assert-SmokePlainTree -Path $path
    $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
        if($stream.Length -lt 1 -or $stream.Length -gt 131072){throw 'Supervisor review report outside bound.'}
        $bytes=New-Object byte[] ([int]$stream.Length);$offset=0
        while($offset -lt $bytes.Length){$count=$stream.Read($bytes,$offset,$bytes.Length-$offset);if($count -le 0){throw 'Supervisor review report incomplete.'};$offset+=$count}
        if((Get-SmokeSupervisorDigest $bytes) -cne $Reference.sha256){throw 'Supervisor review report hash differs.'}
        $encoding=New-Object Text.UTF8Encoding($false,$true)
        $text=$encoding.GetString($bytes)
        if([string]::IsNullOrWhiteSpace($text)){throw 'Supervisor review report empty.'}
        return $text
    }finally{$stream.Dispose()}
}
function Assert-SmokeSupervisorReviewedRecipe($Reference,$Closure,$HostIdentity,[string]$Producer){
    $recipe=Read-SmokeSupervisorArtifact $Reference $Closure.Root
    Assert-SmokeSupervisorFields $recipe.Value @('schema','kind','bundle_sha256','host_key','driver','cases','review')
    $cases=@('creation-job','ipc-transfer-ack','controller-exit-adoption','late-accounting','collision-conflict','native-closeout')
    if($recipe.Value.schema -cne 'pd2.receipt-supervisor-recipe.v1' -or $recipe.Value.kind -cne 'real-windows-native' -or
        $recipe.Value.bundle_sha256 -cne $Closure.Sha256 -or $recipe.Value.host_key -cne $HostIdentity.Key -or
        (@($recipe.Value.cases) -join ',') -cne ($cases -join ',')){throw 'Supervisor native recipe differs.'}
    $driver=Read-SmokeSupervisorArtifact $recipe.Value.driver $Closure.Root
    Assert-SmokeSupervisorFields $driver.Value @('schema','sources')
    if($driver.Value.schema -cne 'pd2.receipt-supervisor-driver-sources.v1' -or
        @($driver.Value.sources).Count -lt 1 -or @($driver.Value.sources).Count -gt 8){throw 'Supervisor driver manifest differs.'}
    $seen=@{}
    foreach($source in @($driver.Value.sources)){
        Assert-SmokeSupervisorFields $source @('path','sha256')
        if($source.path -isnot [string] -or $source.path -cnotmatch '^\.claude/[A-Za-z0-9._/-]+\.(ps1|py)$' -or
            $source.path -match '(^|/)\.\.?(/|$)' -or $seen.ContainsKey($source.path)){throw 'Supervisor recipe source path duplicate/invalid.'}
        $seen[$source.path]=$true;$path=Get-SmokeSupervisorArtifactPath $source $Closure.Root
        Assert-SmokePlainTree -Path $path
        if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $source.sha256){throw 'Supervisor recipe source changed.'}
    }
    $review=Read-SmokeSupervisorArtifact $recipe.Value.review $Closure.Root
    Assert-SmokeSupervisorFields $review.Value @('schema','reviewer','model','verdict','bundle_sha256','host_key','driver_sha256','cases','material_findings','report')
    if($review.Value.schema -cne 'pd2.receipt-supervisor-recipe-review.v1' -or $review.Value.verdict -cne 'SOURCE_PLAN_PASS' -or
        $review.Value.reviewer -isnot [string] -or [string]::IsNullOrWhiteSpace($review.Value.reviewer) -or $review.Value.reviewer -ceq $Producer -or
        $review.Value.model -isnot [string] -or [string]::IsNullOrWhiteSpace($review.Value.model) -or
        $review.Value.bundle_sha256 -cne $Closure.Sha256 -or $review.Value.host_key -cne $HostIdentity.Key -or
        $review.Value.driver_sha256 -cne $driver.Sha256 -or (@($review.Value.cases) -join ',') -cne ($cases -join ',')){throw 'Supervisor independent recipe review differs.'}
    Assert-SmokeSupervisorInteger $review.Value.material_findings 0 0
    $report=Read-SmokeSupervisorReport $review.Value.report $Closure.Root
    if(-not $report.Contains($Closure.Sha256) -or -not $report.Contains($HostIdentity.Key) -or
        -not $report.Contains($driver.Sha256) -or -not $report.Contains('SOURCE_PLAN_PASS')){throw 'Supervisor recipe report does not bind reviewed scope.'}
    return [pscustomobject]@{Recipe=$recipe;Driver=$driver;Review=$review;Cases=$cases}
}
function Assert-SmokeSupervisorProof($Record,$Closure,$HostIdentity){
    Assert-SmokeSupervisorFields $Record @('schema','capability','scope','project_root','sources','bundle_sha256','host','host_key','recipe','native_receipt','independent_native_review','release')
    if($Record.schema -cne 'pd2.receipt-supervisor-capability.v1' -or $Record.capability -cne 'receipt-supervisor' -or
        $Record.scope -cne 'receipt-accounting-only' -or $Record.project_root -cne $Closure.Root -or
        $Record.bundle_sha256 -cne $Closure.Sha256 -or $Record.host_key -cne $HostIdentity.Key -or
        (ConvertTo-Json $Record.host -Depth 10 -Compress) -cne (ConvertTo-Json $HostIdentity.Value -Depth 10 -Compress)){throw 'Supervisor capability source/host/scope differs.'}
    Assert-SmokeSupervisorFields $Record.sources @($Closure.Sources.Keys)
    foreach($key in $Closure.Sources.Keys){if($Record.sources.$key -cne $Closure.Sources[$key]){throw 'Supervisor capability source changed.'}}
    $validated=Assert-SmokeSupervisorReviewedRecipe $Record.recipe $Closure $HostIdentity 'codex-pd-local-cue-20261010'
    $recipe=$validated.Recipe;$cases=$validated.Cases
    $native=Read-SmokeSupervisorArtifact $Record.native_receipt $Closure.Root
    Assert-SmokeSupervisorFields $native.Value @('schema','kind','bundle_sha256','host_key','recipe_sha256','cases','started_utc','completed_utc')
    if($native.Value.schema -cne 'pd2.receipt-supervisor-native.v1' -or $native.Value.kind -cne 'real-windows-native' -or
        $native.Value.bundle_sha256 -cne $Closure.Sha256 -or $native.Value.host_key -cne $HostIdentity.Key -or
        $native.Value.recipe_sha256 -cne $recipe.Sha256){throw 'Supervisor native evidence binding differs.'}
    $nativeStart=Read-SmokeSupervisorUtc $native.Value.started_utc
    $nativeEnd=Read-SmokeSupervisorUtc $native.Value.completed_utc
    if($nativeEnd -lt $nativeStart -or $nativeEnd -gt [DateTimeOffset]::UtcNow){throw 'Supervisor native chronology differs.'}
    Assert-SmokeSupervisorFields $native.Value.cases $cases
    foreach($name in $cases){
        $case=$native.Value.cases.$name
        Assert-SmokeSupervisorFields $case @('verdict','evidence')
        if($case.verdict -cne 'NATIVE_PASS'){throw 'Supervisor native case not complete/passing.'}
        $evidence=Read-SmokeSupervisorArtifact $case.evidence $Closure.Root
        Assert-SmokeSupervisorFields $evidence.Value @('schema','case','kind','bundle_sha256','host_key','recipe_sha256','native_pid','creation_ticks','operation','executable_sha256','root_exited','domain_active','handles_closed','facts','transcript')
        Assert-SmokeSupervisorInteger $evidence.Value.native_pid 1 ([int]::MaxValue)
        Assert-SmokeSupervisorInteger $evidence.Value.creation_ticks
        Assert-SmokeSupervisorInteger $evidence.Value.domain_active 0 0
        if($evidence.Value.schema -cne 'pd2.receipt-supervisor-native-case.v1' -or $evidence.Value.case -cne $name -or
            $evidence.Value.kind -cne 'real-windows-native' -or $evidence.Value.bundle_sha256 -cne $Closure.Sha256 -or
            $evidence.Value.host_key -cne $HostIdentity.Key -or $evidence.Value.recipe_sha256 -cne $recipe.Sha256 -or
            $evidence.Value.executable_sha256 -cne $HostIdentity.Value.binaries[$HostIdentity.Value.python_path] -or
            $evidence.Value.operation -isnot [string] -or $evidence.Value.operation -cnotmatch '^[0-9a-f]{32}$' -or
            $evidence.Value.root_exited -isnot [bool] -or -not $evidence.Value.root_exited -or
            $evidence.Value.domain_active -isnot [int] -or $evidence.Value.domain_active -ne 0 -or
            $evidence.Value.handles_closed -isnot [bool] -or -not $evidence.Value.handles_closed){throw 'Supervisor native case lacks physical closeout/facts.'}
        Assert-SmokeSupervisorNativeFacts $name $evidence.Value.facts $evidence.Value
        $transcript=Read-SmokeSupervisorArtifact $evidence.Value.transcript $Closure.Root
        Assert-SmokeSupervisorFields $transcript.Value @('schema','kind','case','bundle_sha256','host_key','recipe_sha256','operation','native_pid','creation_ticks','facts','events')
        Assert-SmokeSupervisorInteger $transcript.Value.native_pid 1 ([int]::MaxValue)
        Assert-SmokeSupervisorInteger $transcript.Value.creation_ticks
        if($transcript.Value.schema -cne 'pd2.receipt-supervisor-native-transcript.v1' -or
            $transcript.Value.kind -cne 'real-windows-native' -or $transcript.Value.case -cne $name -or
            $transcript.Value.bundle_sha256 -cne $Closure.Sha256 -or $transcript.Value.host_key -cne $HostIdentity.Key -or
            $transcript.Value.recipe_sha256 -cne $recipe.Sha256 -or $transcript.Value.operation -cne $evidence.Value.operation -or
            $transcript.Value.native_pid -ne $evidence.Value.native_pid -or $transcript.Value.creation_ticks -ne $evidence.Value.creation_ticks -or
            (ConvertTo-Json $transcript.Value.facts -Depth 20 -Compress) -cne (ConvertTo-Json $evidence.Value.facts -Depth 20 -Compress) -or
            $transcript.Value.events -isnot [array] -or @($transcript.Value.events).Count -lt 1 -or
            @($transcript.Value.events).Count -gt 128){throw 'Supervisor actual transcript binding differs.'}
    }
    $review=Read-SmokeSupervisorArtifact $Record.independent_native_review $Closure.Root
    Assert-SmokeSupervisorFields $review.Value @('schema','reviewer','model','verdict','bundle_sha256','host_key','native_receipt_sha256','material_findings','report')
    if($review.Value.schema -cne 'pd2.receipt-supervisor-native-review.v1' -or $review.Value.verdict -cne 'NATIVE_PASS' -or
        $review.Value.bundle_sha256 -cne $Closure.Sha256 -or $review.Value.host_key -cne $HostIdentity.Key -or
        $review.Value.native_receipt_sha256 -cne $native.Sha256 -or $review.Value.material_findings -isnot [int] -or
        $review.Value.material_findings -ne 0 -or $review.Value.reviewer -isnot [string] -or -not $review.Value.reviewer -or
        $review.Value.model -isnot [string] -or [string]::IsNullOrWhiteSpace($review.Value.model) -or
        [string]::IsNullOrWhiteSpace($review.Value.reviewer)){throw 'Supervisor independent native review differs.'}
    $report=Read-SmokeSupervisorReport $review.Value.report $Closure.Root
    if(-not $report.Contains('NATIVE_PASS') -or -not $report.Contains($Closure.Sha256) -or
        -not $report.Contains($HostIdentity.Key) -or -not $report.Contains($native.Sha256)){throw 'Supervisor native report scope differs.'}
    $release=Read-SmokeSupervisorArtifact $Record.release $Closure.Root
    Assert-SmokeSupervisorFields $release.Value @('schema','producer','verdict','bundle_sha256','host_key','native_receipt_sha256','independent_review_sha256','authority')
    if($release.Value.schema -cne 'pd2.receipt-supervisor-release.v1' -or $release.Value.verdict -cne 'APPROVED' -or
        $release.Value.bundle_sha256 -cne $Closure.Sha256 -or $release.Value.host_key -cne $HostIdentity.Key -or
        $release.Value.native_receipt_sha256 -cne $native.Sha256 -or $release.Value.independent_review_sha256 -cne $review.Sha256 -or
        $release.Value.producer -cne 'codex-pd-local-cue-20261010' -or
        $release.Value.producer -ceq $review.Value.reviewer){throw 'Supervisor integrated capability release differs.'}
    $authority=Read-SmokeSupervisorArtifact $release.Value.authority $Closure.Root
    Assert-SmokeSupervisorFields $authority.Value @('schema','actor','verdict','scope','bundle_sha256','host_key','native_receipt_sha256','independent_review_sha256','report')
    if($authority.Value.schema -cne 'pd2.integrated-native-capability-authority.v1' -or
        $authority.Value.actor -cne '01a1281d-619f-728b-87e7-481ab1d8e00c' -or $authority.Value.verdict -cne 'APPROVED' -or
        $authority.Value.scope -cne 'receipt-accounting-only' -or $authority.Value.bundle_sha256 -cne $Closure.Sha256 -or
        $authority.Value.host_key -cne $HostIdentity.Key -or $authority.Value.native_receipt_sha256 -cne $native.Sha256 -or
        $authority.Value.independent_review_sha256 -cne $review.Sha256){throw 'Supervisor integrated authority artifact differs.'}
    $integrated=Read-SmokeSupervisorReport $authority.Value.report $Closure.Root
    if(-not $integrated.Contains('APPROVED') -or -not $integrated.Contains($Closure.Sha256) -or
        -not $integrated.Contains($HostIdentity.Key) -or -not $integrated.Contains($native.Sha256) -or
        -not $integrated.Contains($review.Sha256)){throw 'Supervisor integrated report scope differs.'}
    # Data integrity supplements the trusted producer/independent-review
    # workflow. JSON assertions alone do not prove that native execution occurred.
}
function Assert-SmokeSupervisorNativeFacts([string]$Name,$Facts,$Evidence){
    $fields=switch($Name){
        'creation-job' {@('creation_job_membership','inherited_job_membership','creation_flags')}
        'ipc-transfer-ack' {@('pipe_server_pid','pipe_server_creation_ticks','transferred_root_creation_ticks','transferred_job_membership','ack_sha256','nonce','lost_ack_refused','wrong_generation_refused','foreign_client_refused')}
        'controller-exit-adoption' {@('original_pid','original_creation_ticks','original_root_exited','broker_survived_original_exit','new_controller_pid','new_controller_creation_ticks','old_live_controller_refused')}
        'late-accounting' {@('deadline_expired_unknown','original_operation_reused','publication_count','immutable_value_sha256','accounting_complete')}
        'collision-conflict' {@('durable_collision_refused','job_collision_refused','source_conflict_refused','value_conflict_refused','dispatch_count','foreign_domain_not_controlled')}
        'native-closeout' {@('exit_code','job_zero','checked_disposal','handles_closed','output_mode')}
        default {throw 'Supervisor native case unknown.'}
    }
    Assert-SmokeSupervisorFields $Facts $fields
    $numeric=@('creation_flags','pipe_server_pid','pipe_server_creation_ticks','transferred_root_creation_ticks','original_pid','original_creation_ticks','new_controller_pid','new_controller_creation_ticks','publication_count','dispatch_count','exit_code')
    $textual=@('ack_sha256','nonce','immutable_value_sha256','output_mode')
    foreach($field in $fields){
        if($field -in $numeric){Assert-SmokeSupervisorInteger $Facts.$field $(if($field -eq 'exit_code'){0}else{1})}
        elseif($field -in $textual){if($Facts.$field -isnot [string]){throw 'Supervisor native text fact differs.'}}
        elseif($Facts.$field -isnot [bool] -or -not $Facts.$field){throw 'Supervisor required physical native fact absent/false.'}
    }
    switch($Name){
        'creation-job' {if($Facts.creation_flags -ne 134742016){throw 'Supervisor creation-time flags differ.'}}
        'ipc-transfer-ack' {
            if($Facts.pipe_server_pid -ne $Evidence.native_pid -or $Facts.pipe_server_creation_ticks -ne $Evidence.creation_ticks -or
                $Facts.transferred_root_creation_ticks -ne $Evidence.creation_ticks -or $Facts.ack_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
                $Facts.nonce -cnotmatch '^[0-9a-f]{32}$'){throw 'Supervisor actual pipe/witness/ACK facts differ.'}
        }
        'controller-exit-adoption' {
            if($Facts.original_pid -eq $Facts.new_controller_pid -and $Facts.original_creation_ticks -eq $Facts.new_controller_creation_ticks){throw 'Supervisor adoption lacks distinct controller generation.'}
        }
        'late-accounting' {if($Facts.publication_count -ne 1 -or $Facts.immutable_value_sha256 -cnotmatch '^[0-9a-f]{64}$'){throw 'Supervisor once-only late accounting facts differ.'}}
        'collision-conflict' {if($Facts.dispatch_count -ne 1){throw 'Supervisor conflict redispatch count differs.'}}
        'native-closeout' {if($Facts.exit_code -ne 0 -or $Facts.output_mode -cne 'nul'){throw 'Supervisor final exit/output facts differ.'}}
    }
}
function Assert-SmokeRunnerSupervisorCapability {
    $closure=Get-SmokeRunnerSupervisorSources;$hostIdentity=Get-SmokeSupervisorHost
    $id=(Get-SmokeSupervisorDigest ([Text.Encoding]::UTF8.GetBytes($closure.Sha256+[char]10+$hostIdentity.Key))).Substring(0,32)
    $path=Join-Path $closure.Root ('.claude/smoke-storage/receipts/runner-'+$id+'/supervisor-capability.json')
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw 'Receipt supervisor native capability is closed; immutable qualification record absent.'}
    $record=Read-SmokeSupervisorJson $path
    $owner=(Read-SmokeSupervisorJson (Join-Path (Split-Path -Parent $path) 'run-owner.json')).Value
    Assert-SmokeSupervisorFields $owner @('schema','run_id','owner','retain_housekeeping')
    Assert-SmokeSupervisorFields $owner.owner @('pid','creation_ticks')
    Assert-SmokeSupervisorInteger $owner.owner.pid 1 ([int]::MaxValue)
    Assert-SmokeSupervisorInteger $owner.owner.creation_ticks
    if($owner.schema -cne 'pd2.smoke-runner-retention.v1' -or $owner.run_id -cne ('runner-'+$id) -or
        $owner.retain_housekeeping -isnot [bool] -or -not $owner.retain_housekeeping){throw 'Supervisor capability lacks its exact accounted owner context.'}
    Assert-SmokeSupervisorProof $record.Value $closure $hostIdentity
    $closure|Add-Member -NotePropertyName HostIdentity -NotePropertyValue $hostIdentity
    return $closure
}
function Start-SmokeRunnerReceiptOperation($Descriptor){
    return Start-SmokeRunnerReceiptOperationCore $Descriptor
}
function Assert-SmokeSupervisorQualification($Descriptor,[string]$AdmissionPath,[string]$Action){
    if($Descriptor.Spec.qualification_required -isnot [bool] -or -not $Descriptor.Spec.qualification_required){throw 'Qualification descriptor must require broker-side finite admission.'}
    $closure=Get-SmokeRunnerSupervisorSources;$hostIdentity=Get-SmokeSupervisorHost
    $path=[IO.Path]::GetFullPath($AdmissionPath)
    if(-not $path.StartsWith($closure.Root+'\.claude\pd-retained-native-candidate-20261012\parent-closeout-v1\',[StringComparison]::Ordinal)){throw 'Qualification admission outside assigned recipe namespace.'}
    $admission=(Read-SmokeSupervisorJson $path).Value
    Assert-SmokeSupervisorFields $admission @('schema','actor','project_root','bundle_sha256','host_key','recipe','controller_pid','controller_creation_ticks','parent_run','operation','queue_id','started_utc','expires_utc','action','machine_admission')
    $controller=[Diagnostics.Process]::GetCurrentProcess()
    try{$ticks=$controller.StartTime.ToUniversalTime().ToFileTimeUtc()}finally{$controller.Dispose()}
    if($admission.schema -cne 'pd2.receipt-supervisor-qualification-admission.v1' -or
        $admission.actor -cne 'codex-pd-local-cue-20261010' -or $admission.project_root -cne $closure.Root -or
        $admission.bundle_sha256 -cne $closure.Sha256 -or $admission.host_key -cne $hostIdentity.Key -or
        $admission.controller_pid -isnot [int] -or $admission.controller_pid -ne $PID -or
        $admission.controller_creation_ticks -ne $ticks -or $admission.parent_run -cne $Descriptor.Spec.parent_run -or
        $admission.operation -cne $Descriptor.Spec.operation -or $admission.action -cne $Action){throw 'Qualification admission identity/action/source differs.'}
    Assert-SmokeSupervisorInteger $admission.controller_creation_ticks
    $start=Read-SmokeSupervisorUtc $admission.started_utc;$end=Read-SmokeSupervisorUtc $admission.expires_utc;$now=[DateTimeOffset]::UtcNow
    if($start.Offset -ne [TimeSpan]::Zero -or $end.Offset -ne [TimeSpan]::Zero -or $start -gt $now -or $end -le $now -or
        ($end-$start).TotalSeconds -gt 600 -or ($end-$start).TotalSeconds -le 0){throw 'Qualification finite window absent/expired.'}
    $hub=Read-SmokeSupervisorJson (Join-Path $closure.Root '.codex-coordination/state.json') 4194304 -OperatorDocument
    # Hub state is operator JSON, not canonical producer JSON. It is read below
    # through the same bounded held-byte reader with lossless checking disabled
    # only for this non-capability operational document.
    $queue=@($hub.Value.queue|Where-Object {$_.id -ceq $admission.queue_id})
    if($queue.Count -ne 1 -or $queue[0].sessionId -cne $admission.actor -or $queue[0].status -cne 'running' -or
        $queue[0].resource -cne 'test-runner'){throw 'Qualification lacks current exact running FIFO.'}
    $policy=Get-Content -LiteralPath (Join-Path $closure.Root 'tools/smoke-verify/storage-policy.json') -Raw|ConvertFrom-Json
    Assert-SmokeSupervisorFields ([pscustomobject]$Descriptor.Spec.policy) @($policy.PSObject.Properties|ForEach-Object Name)
    foreach($property in $policy.PSObject.Properties){
        if((ConvertTo-Json $Descriptor.Spec.policy[$property.Name] -Compress) -cne (ConvertTo-Json $property.Value -Compress)){throw 'Qualification cannot weaken or coerce production storage policy.'}
    }
    if($Descriptor.Spec.project -cne $closure.Root){throw 'Qualification requires canonical accounted context.'}
    $machine=Read-SmokeSupervisorArtifact $admission.machine_admission $closure.Root
    Assert-SmokeSupervisorFields $machine.Value @('schema','actor','host_key','observed_utc','free_physical_mib','available_commit_mib','scope','other_heavy_owners_reconciled')
    $observed=Read-SmokeSupervisorUtc $machine.Value.observed_utc
    Assert-SmokeSupervisorMemory $machine.Value.free_physical_mib 2048
    Assert-SmokeSupervisorMemory $machine.Value.available_commit_mib 4096
    if($machine.Value.schema -cne 'pd2.finite-pd-machine-admission.v1' -or $machine.Value.actor -cne $admission.actor -or
        $machine.Value.host_key -cne $hostIdentity.Key -or $machine.Value.scope -cne 'receipt-supervisor-only' -or
        $machine.Value.other_heavy_owners_reconciled -isnot [bool] -or -not $machine.Value.other_heavy_owners_reconciled -or
        $machine.Value.free_physical_mib -lt 2048 -or $machine.Value.available_commit_mib -lt 4096 -or
        $observed -gt $now -or ($now-$observed).TotalSeconds -gt 30){throw 'Qualification lacks fresh finite machine/memory admission.'}
    $validated=Assert-SmokeSupervisorReviewedRecipe $admission.recipe $closure $hostIdentity $admission.actor
    $now=[DateTimeOffset]::UtcNow
    if($end -le $now -or ($now-$observed).TotalSeconds -gt 30){throw 'Qualification expired during source/recipe validation.'}
    $hub=Read-SmokeSupervisorJson (Join-Path $closure.Root '.codex-coordination/state.json') 4194304 -OperatorDocument
    $queue=@($hub.Value.queue|Where-Object {$_.id -ceq $admission.queue_id})
    if($queue.Count -ne 1 -or $queue[0].sessionId -cne $admission.actor -or $queue[0].status -cne 'running' -or
        $queue[0].resource -cne 'test-runner'){throw 'Qualification FIFO changed during validation.'}
    $expiry=[math]::Min($end.UtcDateTime.Ticks,$observed.AddSeconds(30).UtcDateTime.Ticks)
    $closure|Add-Member -NotePropertyName AdmissionExpiryTicks -NotePropertyValue ([long]$expiry)
    $closure|Add-Member -NotePropertyName QueueDocumentPath -NotePropertyValue $hub.Path
    $closure|Add-Member -NotePropertyName QueueDocumentSha256 -NotePropertyValue $hub.Sha256
    $closure|Add-Member -NotePropertyName HostIdentity -NotePropertyValue $hostIdentity
    return $closure
}
function Start-SmokeRunnerReceiptQualification($Descriptor,[string]$AdmissionPath){
    # This entry grants only the reviewed tiny accounting operation, never a
    # build/game/capture or queue release. It uses the identical native core.
    return Start-SmokeRunnerReceiptOperationCore $Descriptor $AdmissionPath
}
function Renew-SmokeRunnerReceiptQualification($Operation,[string]$AdmissionPath){
    # Renew only this retained controller/operation, never dispatch or adopt a
    # second worker. Consume finished RPC results before changing authorization.
    if($Operation.Complete -or $null -ne $Operation.Rpc -or
        ($null -ne $Operation.Startup -and (-not $Operation.Startup.IsCompleted -or $Operation.Startup.IsFaulted))){throw 'Qualification renewal requires the retained idle original operation.'}
    $controller=[Diagnostics.Process]::GetCurrentProcess()
    try{$created=$controller.StartTime.ToUniversalTime().ToFileTimeUtc()}finally{$controller.Dispose()}
    if($Operation.Native.ControllerPid -ne $PID -or $Operation.Native.ControllerCreationTicks -ne $created -or
        $Operation.Native.Operation -cne $Operation.Descriptor.Spec.operation){throw 'Qualification renewal controller/operation differs.'}
    if($null -ne $Operation.Descriptor.Raw -and (Get-SmokeSupervisorDigest $Operation.Descriptor.Raw) -cne $Operation.Descriptor.Spec.request_sha256){throw 'Qualification renewal immutable request changed.'}
    $current=Assert-SmokeSupervisorQualification $Operation.Descriptor $AdmissionPath 'receipt-renew'
    if($Operation.Descriptor.SourceRoot -cne $current.Root -or $Operation.Descriptor.Spec.project -cne $current.Root){throw 'Qualification renewal canonical context differs.'}
    Assert-SmokeSupervisorFields ([pscustomobject]$Operation.Descriptor.Spec.sources) @($current.Sources.Keys)
    foreach($key in $current.Sources.Keys){if($Operation.Descriptor.Spec.sources[$key] -cne $current.Sources[$key]){throw 'Qualification renewal source changed.'}}
    $Operation.Native.SetAttachmentAdmission($current.AdmissionExpiryTicks,$current.QueueDocumentPath,$current.QueueDocumentSha256)
    return $Operation
}
function Start-SmokeRunnerReceiptOperationCore($Descriptor,[string]$QualificationAdmission=''){
    $current=if($QualificationAdmission){Assert-SmokeSupervisorQualification $Descriptor $QualificationAdmission 'receipt-start'}else{Assert-SmokeRunnerSupervisorCapability}
    if($Descriptor.SourceRoot -cne $current.Root -or $Descriptor.Spec.project -cne $current.Root -or
        $Descriptor.Spec.schema -cne 'pd2.receipt-supervisor.v1' -or
        $Descriptor.Spec.operation -cnotmatch '^[0-9a-f]{32}$'){throw 'Supervisor descriptor canonical identity differs.'}
    Assert-SmokeSupervisorFields ([pscustomobject]$Descriptor.Spec) @('schema','qualification_required','operation','project','policy','parent_run','parent_owner','name','request_sha256','sources')
    if($Descriptor.Spec.qualification_required -isnot [bool] -or $Descriptor.Spec.qualification_required -ne [bool]$QualificationAdmission){throw 'Supervisor qualification authority cannot switch across entries.'}
    Assert-SmokeSupervisorFields ([pscustomobject]$Descriptor.Spec.parent_owner) @('pid','creation_ticks')
    Assert-SmokeSupervisorInteger $Descriptor.Spec.parent_owner.pid 1 ([int]::MaxValue)
    Assert-SmokeSupervisorInteger $Descriptor.Spec.parent_owner.creation_ticks
    $process=[Diagnostics.Process]::GetCurrentProcess()
    try{$creation=$process.StartTime.ToUniversalTime().ToFileTimeUtc()}finally{$process.Dispose()}
    if($Descriptor.Spec.parent_owner.pid -ne $PID -or $Descriptor.Spec.parent_owner.creation_ticks -ne $creation -or
        $Descriptor.Spec.parent_run -cnotmatch '^runner-[0-9a-f]{32}$' -or
        $Descriptor.Spec.name -cnotmatch '^parent-closeout-[0-9a-f]{32}(-completion)?\.json$' -or
        $Descriptor.Raw -isnot [byte[]] -or $Descriptor.Raw.Length -gt 33554432 -or
        $Descriptor.Spec.request_sha256 -cne (Get-SmokeSupervisorDigest $Descriptor.Raw)){throw 'Supervisor live parent/payload binding differs.'}
    $id=(Get-SmokeSupervisorDigest ([Text.Encoding]::UTF8.GetBytes($Descriptor.Spec.parent_run+[char]10+$Descriptor.Spec.name))).Substring(0,32)
    if($id -cne $Descriptor.Spec.operation -or
        $Descriptor.ReceiptRoot -cne (Join-Path $current.Root ('.claude/smoke-storage/receipts/runner-'+$id))){throw 'Supervisor exact operation namespace differs.'}
    Assert-SmokeSupervisorFields ([pscustomobject]$Descriptor.Spec.sources) @($current.Sources.Keys)
    foreach($key in $current.Sources.Keys){if($Descriptor.Spec.sources[$key] -cne $current.Sources[$key]){throw 'Receipt supervisor descriptor source changed.'}}
    $helper=Join-Path $Descriptor.SourceRoot 'tools/smoke-verify/runner_supervisor.py'
    Assert-SmokePlainTree -Path $Descriptor.ReceiptRoot
    if(Test-Path -LiteralPath $Descriptor.ReceiptRoot){throw 'Supervisor operation already has durable evidence; use explicit recovery, never redispatch.'}
    Initialize-SmokeRunnerSupervisorType
    # Managed compilation can take time: repeat the finite admission at the
    # final scheduling boundary, then carry its expiry and FIFO snapshot to C#.
    $current=if($QualificationAdmission){Assert-SmokeSupervisorQualification $Descriptor $QualificationAdmission 'receipt-start'}else{Assert-SmokeRunnerSupervisorCapability}
    $python=$current.HostIdentity.Value.python_path
    $helper=Join-Path $current.Root 'tools/smoke-verify/runner_supervisor.py'
    $native=[PdReceiptSupervisor]::new($Descriptor.Spec.operation,$python)
    $encoded=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((ConvertTo-Json $Descriptor.Spec -Depth 50 -Compress)))
    $operation=[pscustomobject]@{Descriptor=$Descriptor;Native=$native;Startup=$null;Rpc=$null;Action='attach';Reply=$null;
        Verification=$null;Terminal=$null;Shutdown=$false;CloseTask=$null;Complete=$false;Result=$null}
    $expiry=if($QualificationAdmission){$current.AdmissionExpiryTicks}else{[DateTime]::UtcNow.AddSeconds(5).Ticks}
    $queuePath=if($QualificationAdmission){$current.QueueDocumentPath}else{''}
    $queueHash=if($QualificationAdmission){$current.QueueDocumentSha256}else{''}
    if($QualificationAdmission){$native.SetAttachmentAdmission($expiry,$queuePath,$queueHash)}
    $operation.Startup=$native.BeginStart($helper,$encoded,$Descriptor.Raw,$current.Root,$expiry,$queuePath,$queueHash)
    return $operation
}

function Get-SmokeRunnerReceiptMessage($Operation,[string]$Action){
    $spec=$Operation.Descriptor.Spec
    $message=@{action=$Action;operation=$spec.operation;parent_run=$spec.parent_run;name=$spec.name;sources=$spec.sources;
        controller_identity=@{pid=$Operation.Native.ControllerPid;creation_ticks=$Operation.Native.ControllerCreationTicks}}
    if($Action -ne 'describe'){$message.request_sha256=$spec.request_sha256}
    if($null -ne $Operation.Reply -and $Operation.Reply.handles){$message.handles=$Operation.Reply.handles}
    return ConvertTo-Json $message -Depth 20 -Compress
}

function Read-SmokeRunnerReceiptOperationCore($Operation){
    if($Operation.Complete){return $Operation.Result}
    if(-not $Operation.PSObject.Properties.Match('ShutdownAttempted').Count){$Operation|Add-Member -NotePropertyName ShutdownAttempted -NotePropertyValue $false}
    if($null -ne $Operation.Startup){
        if(-not $Operation.Startup.IsCompleted){return}
        if($Operation.Startup.IsFaulted){
            if(-not $Operation.PSObject.Properties.Match('FailedSetupTask').Count){
                $Operation|Add-Member -NotePropertyName FailedSetupTask -NotePropertyValue ($Operation.Native.BeginFailedSetupCloseout())
            }elseif($Operation.FailedSetupTask.IsCompleted -and $Operation.FailedSetupTask.IsFaulted){
                # Read's outer diagnostic recorder retains the failed task's
                # cause before this next explicit call retries remaining handles.
                $Operation.FailedSetupTask=$Operation.Native.BeginFailedSetupCloseout()
            }
            if(-not $Operation.FailedSetupTask.IsCompleted){return}
            $setupProof=$Operation.FailedSetupTask.GetAwaiter().GetResult()
            if(-not $Operation.PSObject.Properties.Match('FailedSetupCloseout').Count){
                $Operation|Add-Member -NotePropertyName FailedSetupCloseout -NotePropertyValue $setupProof
            }
            # Physical no-child cleanup is distinct from accounting completion.
            # Keep the original failed startup and request; no redispatch, normal
            # success proof, inferred publication or automatic queue release.
            [void]$Operation.Startup.GetAwaiter().GetResult()
        }
        [void]$Operation.Startup.GetAwaiter().GetResult();$Operation.Startup=$null
    }
    # A shutdown acknowledgement can be consumed just before its caller loses
    # the transport result. Held, validated native exit remains authoritative;
    # inspect it before retrying that same handoff. Never create a new owner.
    if($null -eq $Operation.Rpc -and $Operation.ShutdownAttempted -and -not $Operation.Shutdown){
        if($null -eq $Operation.CloseTask){$Operation.CloseTask=$Operation.Native.BeginCloseout()}
        if(-not $Operation.CloseTask.IsCompleted){return}
        $recovered=$Operation.CloseTask.GetAwaiter().GetResult();$Operation.CloseTask=$null
        if($null -ne $recovered){return Complete-SmokeRunnerSupervisorCloseout $Operation $recovered}
        $Operation.ShutdownAttempted=$false
    }
    if($null -eq $Operation.Rpc -and -not $Operation.Shutdown){
        if($Operation.Action -eq 'shutdown'){$Operation.ShutdownAttempted=$true}
        $Operation.Rpc=$Operation.Native.Exchange((Get-SmokeRunnerReceiptMessage $Operation $Operation.Action))
    }
    if($null -ne $Operation.Rpc){
        if(-not $Operation.Rpc.IsCompleted){return}
        $reply=$Operation.Rpc.GetAwaiter().GetResult()|ConvertFrom-Json;$Operation.Rpc=$null
        if($reply.PSObject.Properties.Match('error').Count){throw ('Supervisor refused: '+$reply.error)}
        if($reply.schema -cne 'pd2.receipt-supervisor.v1' -or $reply.operation -cne $Operation.Descriptor.Spec.operation -or $reply.output_mode -cne 'nul') {throw 'Supervisor reply contract mismatch.'}
        if($reply.identity.pid -ne $Operation.Native.RootPid -or $reply.identity.creation_ticks -ne $Operation.Native.CreationTicks){throw 'Supervisor reply differs from held pipe server identity.'}
        if($Operation.Action -eq 'describe'){
            if(-not $reply.metadata_only -or $reply.spec.operation -cne $Operation.Descriptor.Spec.operation -or
               $reply.spec.parent_run -cne $Operation.Descriptor.Spec.parent_run -or $reply.spec.name -cne $Operation.Descriptor.Spec.name -or
               $reply.spec.project -cne $Operation.Descriptor.Spec.project -or $reply.spec.request_sha256 -cnotmatch '^[0-9a-f]{64}$'){throw 'Recovery metadata contract mismatch.'}
            foreach($key in $Operation.Descriptor.Spec.sources.Keys){
                if($reply.spec.sources.$key -cne $Operation.Descriptor.Spec.sources[$key]){throw 'Recovery source closure mismatch.'}
            }
            if($reply.spec.parent_owner.pid -eq $PID){throw 'Original controller must resume its retained operation object.'}
            # Original parent marker is immutable; no live-owner substitution.
            $parentPath=Join-Path $Operation.Descriptor.Spec.project ('.claude/smoke-storage/receipts/'+$reply.spec.parent_run+'/run-owner.json')
            Assert-SmokePlainTree -Path $parentPath
            $parent=Get-Content -LiteralPath $parentPath -Raw|ConvertFrom-Json
            if($parent.run_id -cne $reply.spec.parent_run -or $parent.owner.pid -ne $reply.spec.parent_owner.pid -or
               $parent.owner.creation_ticks -ne $reply.spec.parent_owner.creation_ticks){throw 'Recovery original parent marker mismatch.'}
            $Operation.Descriptor.Spec=$reply.spec;$Operation.Action='attach';return
        }
        if($reply.controller.pid -ne $Operation.Native.ControllerPid -or $reply.controller.creation_ticks -ne $Operation.Native.ControllerCreationTicks){throw 'Supervisor reply controller generation mismatch.'}
        $Operation.Native.AcceptWitnesses([long]$reply.handles.root,[long]$reply.handles.job,[int]$reply.identity.pid,[long]$reply.identity.creation_ticks)
        $Operation.Reply=$reply
        if($Operation.Action -eq 'shutdown'){
            if(-not $reply.shutdown_ready){throw 'Supervisor shutdown not acknowledged.'}
            $Operation.Shutdown=$true
        }elseif($reply.terminal){
            # This is executed inside the already-tracked parent runspace. Its
            # I/O can outlive the finite waiter without losing the supervisor.
            $spec=$Operation.Descriptor.Spec
            $path=Join-Path $Operation.Descriptor.ReceiptRoot 'operation-result.json'
            if($reply.terminal.path -cne $path){throw 'Supervisor terminal path mismatch.'}
            Assert-SmokePlainTree -Path $path
            if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $reply.terminal.sha256){throw 'Supervisor terminal bytes changed.'}
            $terminal=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
            if($terminal.schema -cne 'pd2.receipt-supervisor.v1' -or
               (ConvertTo-Json $terminal.spec -Depth 50 -Compress) -cne (ConvertTo-Json ([pscustomobject]$spec) -Depth 50 -Compress)) {throw 'Supervisor terminal operation mismatch.'}
            if($terminal.supervisor.pid -ne $reply.identity.pid -or $terminal.supervisor.creation_ticks -ne $reply.identity.creation_ticks -or
               $terminal.context.Owner.pid -ne $reply.identity.pid -or $terminal.context.Owner.creation_ticks -ne $reply.identity.creation_ticks -or
               $terminal.context.RunId -cne ('runner-'+$spec.operation)){throw 'Supervisor accounted context owner mismatch.'}
            if($terminal.state -ceq 'published'){
                $receipt=$terminal.receipt;$expected=Join-Path $Operation.Descriptor.ReceiptRoot $spec.name
                if($receipt.Path -cne $expected -or $receipt.RunId -cne $terminal.context.RunId -or $receipt.AccountingDomain -cne 'archive'){throw 'Supervisor requested receipt accounting mismatch.'}
                Assert-SmokePlainTree -Path $expected
                $item=Get-Item -LiteralPath $expected
                if($item.Length -ne $receipt.Bytes -or $item.Length -gt 33554432 -or (Get-FileHash -LiteralPath $expected).Hash.ToLowerInvariant() -cne $receipt.ReceiptSha256){throw 'Supervisor requested receipt bytes mismatch.'}
                if($null -ne $Operation.Descriptor.Raw){
                    $observed=Get-Content -LiteralPath $expected -Raw|ConvertFrom-Json
                    $wanted=[Text.Encoding]::UTF8.GetString($Operation.Descriptor.Raw)|ConvertFrom-Json
                    if((ConvertTo-Json $observed -Depth 50 -Compress) -cne (ConvertTo-Json $wanted -Depth 50 -Compress)){throw 'Supervisor requested receipt value mismatch.'}
                }
            }
            $Operation.Terminal=$terminal;$Operation.Action='shutdown'
        }else{$Operation.Action='status'}
    }
    if($Operation.Shutdown){
        if($null -eq $Operation.CloseTask){$Operation.CloseTask=$Operation.Native.BeginCloseout()}
        if(-not $Operation.CloseTask.IsCompleted){return}
        $proof=$Operation.CloseTask.GetAwaiter().GetResult();$Operation.CloseTask=$null
        if($null -eq $proof){$Operation.Shutdown=$false;return}
        return Complete-SmokeRunnerSupervisorCloseout $Operation $proof
    }
}

function Complete-SmokeRunnerSupervisorCloseout($Operation,$proof){
        if($null -eq $Operation.Terminal){throw 'Native exit without validated accounted terminal result.'}
        if($Operation.Terminal.state -cne 'published' -or $proof.ExitCode -ne 0){
            $Operation.Result=[pscustomobject]@{Failure=$Operation.Terminal;SupervisorCloseout=$proof};$Operation.Complete=$true
            throw 'Supervisor preserved an accounted publication failure.'
        }
        $Operation.Result=$Operation.Terminal.receipt
        $Operation.Result|Add-Member -NotePropertyName SupervisorContext -NotePropertyValue $Operation.Terminal.context
        $Operation.Result|Add-Member -NotePropertyName SupervisorCloseout -NotePropertyValue $proof
        $Operation.Result|Add-Member -NotePropertyName ParentRunId -NotePropertyValue $Operation.Descriptor.Spec.parent_run
        $Operation.Complete=$true;return $Operation.Result
}

function Read-SmokeRunnerReceiptOperation($Operation){
    try{return Read-SmokeRunnerReceiptOperationCore $Operation}
    catch{
        $reason=$_.Exception.Message
        $failure=[pscustomobject]@{stage=$Operation.Native.Stage;native_error=$Operation.Native.NativeError;
            reason=$reason.Substring(0,[math]::Min(2048,$reason.Length));observed_utc=[DateTime]::UtcNow.ToString('o')}
        if(-not $Operation.PSObject.Properties.Match('FirstFailure').Count){$Operation|Add-Member -NotePropertyName FirstFailure -NotePropertyValue $failure}
        if($Operation.PSObject.Properties.Match('LastFailure').Count){$Operation.LastFailure=$failure}else{$Operation|Add-Member -NotePropertyName LastFailure -NotePropertyValue $failure}
        if($Operation.Complete){throw}
        # Resume only completed failed transport/close tasks; never replace an
        # unfinished call, native owner or immutable accounting dispatch. Failed
        # startup is retained, not retried under the same operation identity.
        if($null -ne $Operation.Rpc -and $Operation.Rpc.IsCompleted){$Operation.Rpc=$null}
        if($null -ne $Operation.CloseTask -and $Operation.CloseTask.IsCompleted){$Operation.CloseTask=$null}
        return
    }
}

function Resume-SmokeRunnerReceiptOperation($Descriptor){
    # Evidence-only recovery. This API never releases a build/coordination queue.
    # The broker authenticates a new controller only after the original native
    # generation exits; the caller acquires witnesses before any shutdown.
    if($Descriptor.Spec.parent_owner.pid -eq $PID){throw 'Original controller must resume its existing operation object.'}
    return Resume-SmokeRunnerReceiptOperationCore $Descriptor
}
function Resume-SmokeRunnerReceiptQualification($Descriptor,[string]$AdmissionPath){
    return Resume-SmokeRunnerReceiptOperationCore $Descriptor $AdmissionPath
}
function Resume-SmokeRunnerReceiptOperationCore($Descriptor,[string]$QualificationAdmission=''){
    if($Descriptor.Spec.parent_owner.pid -eq $PID){throw 'Original controller must resume its existing operation object.'}
    $current=if($QualificationAdmission){Assert-SmokeSupervisorQualification $Descriptor $QualificationAdmission 'receipt-recover'}else{Assert-SmokeRunnerSupervisorCapability}
    if($Descriptor.SourceRoot -cne $current.Root -or $Descriptor.Spec.project -cne $current.Root -or
        $Descriptor.Spec.operation -cnotmatch '^[0-9a-f]{32}$'){throw 'Supervisor recovery canonical identity differs.'}
    Assert-SmokeSupervisorFields ([pscustomobject]$Descriptor.Spec.sources) @($current.Sources.Keys)
    foreach($key in $current.Sources.Keys){if($Descriptor.Spec.sources.$key -cne $current.Sources[$key]){throw 'Receipt supervisor recovery source changed.'}}
    Initialize-SmokeRunnerSupervisorType
    $current=if($QualificationAdmission){Assert-SmokeSupervisorQualification $Descriptor $QualificationAdmission 'receipt-recover'}else{Assert-SmokeRunnerSupervisorCapability}
    $python=$current.HostIdentity.Value.python_path
    $native=[PdReceiptSupervisor]::new($Descriptor.Spec.operation,$python)
    if($QualificationAdmission){
        $native.SetAttachmentAdmission($current.AdmissionExpiryTicks,$current.QueueDocumentPath,$current.QueueDocumentSha256)
    }
    return [pscustomobject]@{Descriptor=$Descriptor;Native=$native;Startup=$null;Rpc=$null;Action='attach';Reply=$null;
        Verification=$null;Terminal=$null;Shutdown=$false;CloseTask=$null;Complete=$false;Result=$null}
}

function Start-SmokeRunnerReceiptRecovery([string]$ProjectRoot,[string]$ParentRunId,[string]$Name){
    # Metadata-only discovery first; no dispatch/new-run or queue operation.
    # Read/IPC work occurs in the caller's tracked runspace just like publication.
    if($ParentRunId -cnotmatch '^runner-[0-9a-f]{32}$' -or $Name -cnotmatch '^parent-closeout-[0-9a-f]{32}(-completion)?\.json$'){throw 'Invalid recovery parent/name.'}
    $closure=Assert-SmokeRunnerSupervisorCapability
    $project=[IO.Path]::GetFullPath($ProjectRoot)
    if($project -cne $closure.Root -and -not $project.StartsWith(($closure.Root+'\.claude\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Recovery project escaped canonical workspace.'}
    $hash=[Security.Cryptography.SHA256]::Create()
    try{$id=[BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($ParentRunId+"`n"+$Name))).Replace('-','').ToLowerInvariant().Substring(0,32)}finally{$hash.Dispose()}
    Initialize-SmokeRunnerSupervisorType
    $descriptor=[pscustomobject]@{Spec=@{operation=$id;parent_run=$ParentRunId;name=$Name;project=$project;sources=$closure.Sources};Raw=$null;
        SourceRoot=$closure.Root;ReceiptRoot=(Join-Path $project ('.claude/smoke-storage/receipts/runner-'+$id))}
    $python=$closure.HostIdentity.Value.python_path
    $native=[PdReceiptSupervisor]::new($id,$python)
    return [pscustomobject]@{Descriptor=$descriptor;Native=$native;Startup=$null;Rpc=$null;Action='describe';Reply=$null;
        Verification=$null;Terminal=$null;Shutdown=$false;CloseTask=$null;Complete=$false;Result=$null}
}
