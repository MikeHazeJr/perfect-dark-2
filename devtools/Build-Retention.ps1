# Retained wrapper protocol. Existing storage owns archive accounting and locks.
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/smoke-verify/lib/Storage-Harness.ps1')
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/smoke-verify/lib/Runner-Retention.ps1')
$loadedPath=[IO.Path]::GetFullPath($PSCommandPath)
$loadedHash=(Get-FileHash -LiteralPath $loadedPath -Algorithm SHA256).Hash.ToLowerInvariant()
if($script:SmokeReceiptLoadedSources.ContainsKey($loadedPath) -and $script:SmokeReceiptLoadedSources[$loadedPath] -cne $loadedHash){throw 'Build retention module changed in a live interpreter; preserve its original operations.'}
$script:SmokeReceiptLoadedSources[$loadedPath]=$loadedHash

# No downstream source has a released, reviewed retained-build capability yet.
# Populate only in the separately owned/reviewed builder integration cohort.
$script:BuildRetentionReviewedChildSha256=''
foreach($stateName in @('BuildRetentionContext','BuildRetentionChild','BuildRetentionQueueLock',
    'BuildRetentionCapture','BuildRetentionCloseout','BuildRetentionReceiptWorker',
    'BuildRetentionCloseoutOperation','BuildRetentionFailure')){
    if($null -eq (Get-Variable -Name $stateName -Scope Script -ErrorAction SilentlyContinue)){
        Set-Variable -Name $stateName -Scope Script -Value $null
    }
}

# Source-only integration until this exact parent/child pair is qualified on the
# canonical PS5.1 host. No alternate shell, PID scan or ordinary-launch fallback.
function Initialize-BuildParentCaptureType {
    $captureSource=@'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;
public sealed class PdBuildParentCapture {
    public const string SourceSha256="PD_BUILD_SOURCE_SHA256";
    public sealed class Proof {
        public string Schema { get { return "pd2.build-parent-closeout.v1"; } }
        public string RequestId { get; private set; }
        public string RunId { get; private set; }
        public string DomainId { get; private set; }
        public int RootPid { get; private set; }
        public string CreationUtc { get; private set; }
        public string CaptureSourceSha256 { get { return SourceSha256; } }
        public int WrapperPid { get; private set; }
        public bool RootExited { get { return true; } }
        public uint DomainActive { get { return 0; } }
        public bool StdoutEof { get { return true; } }
        public bool StderrEof { get { return true; } }
        public bool Flushed { get { return true; } }
        public int ExitCode { get; private set; }
        internal Proof(PdBuildParentCapture c) {
            RequestId=c.RequestId;RunId=c.RunId;DomainId=c.DomainId;
            RootPid=c.Id;CreationUtc=c.CreationUtc;ExitCode=c.launched?c.ExitCode:1;
            WrapperPid=c.WrapperPid;
        }
    }
    public Proof CloseoutProof { get; private set; }
    public sealed class Data {
        public Proof Proof { get; private set; }
        public string StdoutBase64 { get; private set; }
        public string StderrBase64 { get; private set; }
        internal Data(Proof proof,byte[] stdout,byte[] stderr) {
            Proof=proof;StdoutBase64=Convert.ToBase64String(stdout);StderrBase64=Convert.ToBase64String(stderr);
        }
    }
    public sealed class Failure {
        public string Stage { get; private set; }
        public string Message { get; private set; }
        public int NativeError { get; private set; }
        internal Failure(string stage,Exception e) {
            Stage=stage.Substring(0,Math.Min(128,stage.Length));Message=e.Message.Substring(0,Math.Min(2048,e.Message.Length));
            var native=e as Win32Exception;NativeError=native==null?0:native.NativeErrorCode;
        }
    }
    public Failure LaunchFailure { get; private set; }
    public Failure CleanupFailure { get; private set; }
    [StructLayout(LayoutKind.Sequential)] struct Security { public int Size; public IntPtr Descriptor; public int Inherit; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct Startup {
        public int Size; public string Reserved, Desktop, Title;
        public uint X,Y,XSize,YSize,XChars,YChars,Fill,Flags;
        public ushort Show,ReservedSize; public IntPtr ReservedBytes,Input,Output,Error;
    }
    [StructLayout(LayoutKind.Sequential)] struct StartupEx { public Startup Info; public IntPtr Attributes; }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr Process,Thread; public uint Id,ThreadId; }
    [StructLayout(LayoutKind.Sequential)] struct Accounting {
        public long User,Kernel,PeriodUser,PeriodKernel;
        public uint Faults,Total,Active,Terminated;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateJobObjectW(IntPtr attributes,string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job,int type,out Accounting data,int size,IntPtr length);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool CreatePipe(out IntPtr read,out IntPtr write,ref Security attributes,uint size);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetHandleInformation(IntPtr handle,uint mask,uint flags);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateFileW(string path,uint access,uint share,ref Security attributes,uint disposition,uint flags,IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool InitializeProcThreadAttributeList(IntPtr list,int count,uint flags,ref IntPtr size);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool UpdateProcThreadAttribute(IntPtr list,uint flags,IntPtr key,IntPtr value,IntPtr size,IntPtr previous,IntPtr returned);
    [DllImport("kernel32.dll")] static extern void DeleteProcThreadAttributeList(IntPtr list);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool CreateProcessW(string app,StringBuilder args,IntPtr processAttributes,IntPtr threadAttributes,bool inherit,uint flags,IntPtr environment,string cwd,ref StartupEx startup,out ProcessInfo process);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessTimes(IntPtr process,out long created,out long exited,out long kernel,out long user);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern uint GetCurrentProcessId();
    IntPtr job=IntPtr.Zero, root=IntPtr.Zero;
    // Raw handles stay on the owner through partial setup/disposal failures.
    IntPtr outRead=IntPtr.Zero,outWrite=IntPtr.Zero,errRead=IntPtr.Zero,errWrite=IntPtr.Zero,input=IntPtr.Zero,thread=IntPtr.Zero;
    FileStream output,error,outPipe,errPipe;
    Task outPump,errPump;
    Task<Data> snapshotWork;
    Task disposeWork;
    bool launched,closed;
    public int Id { get; private set; }
    public string CreationUtc { get; private set; }
    public string DomainId { get; private set; }
    public string RequestId { get; private set; }
    public string RunId { get; private set; }
    public string StdoutPath { get; private set; }
    public string StderrPath { get; private set; }
    public int WrapperPid { get; private set; }
    public bool Launched { get { return launched; } }
    static void Require(bool ok,string action) { if(!ok)throw new Win32Exception(Marshal.GetLastWin32Error(),action); }
    public static IntPtr CheckedClose(IntPtr value,Func<IntPtr,bool> close,Func<int> error) {
        if(value==IntPtr.Zero||value==new IntPtr(-1))return IntPtr.Zero;
        if(!close(value))throw new Win32Exception(error(),"Close exact owned handle");
        return IntPtr.Zero;
    }
    static void Close(ref IntPtr value) {
        // Assignment happens only after successful close; a failed call leaves
        // the exact witness available for an owned retry, never a guessed PID.
        value=CheckedClose(value,CloseHandle,Marshal.GetLastWin32Error);
    }
    void CloseStartup() {
        Exception failure=null;
        try{Close(ref outWrite);}catch(Exception e){failure=e;}
        try{Close(ref errWrite);}catch(Exception e){if(failure==null)failure=e;}
        try{Close(ref input);}catch(Exception e){if(failure==null)failure=e;}
        try{Close(ref thread);}catch(Exception e){if(failure==null)failure=e;}
        if(failure!=null){CleanupFailure=new Failure("startup handles",failure);throw failure;}
    }
    public PdBuildParentCapture(string stdout,string stderr,string request,string run) {
        if(String.IsNullOrEmpty(request)||String.IsNullOrEmpty(run))throw new IOException("Missing capture identity");
        RequestId=request;RunId=run;DomainId=Guid.NewGuid().ToString("N");StdoutPath=stdout;StderrPath=stderr;
        WrapperPid=(int)GetCurrentProcessId();
        // FileShare.Read permits tail/progress readers but excludes every writer.
        output=new FileStream(stdout,FileMode.CreateNew,FileAccess.ReadWrite,FileShare.Read,4096,true);
        try { error=new FileStream(stderr,FileMode.CreateNew,FileAccess.ReadWrite,FileShare.Read,4096,true); }
        catch { output.Flush(true);output.Dispose();throw; }
    }
    public static void InvokeCreationBoundary(Action<Action> boundary,Action create) {
        int active=1,dispatched=0;
        int invokingThread=Thread.CurrentThread.ManagedThreadId;
        Action once=()=>{
            if(Thread.CurrentThread.ManagedThreadId!=invokingThread)
                throw new IOException("Capture creation callback must run synchronously on its invoking thread");
            if(Volatile.Read(ref active)!=1)
                throw new IOException("Capture creation callback is no longer active");
            if(Interlocked.CompareExchange(ref dispatched,1,0)!=0)
                throw new IOException("Capture creation callback already dispatched");
            create();
        };
        try {
            if(boundary==null)once();else boundary(once);
            if(Volatile.Read(ref dispatched)!=1)
                throw new IOException("Capture creation boundary did not admit a launch");
        } finally {
            // A retained delegate never outlives the native setup allocations,
            // including guard refusal/exception and successful dispatch.
            Interlocked.Exchange(ref active,0);
        }
    }
    public void Start(string executable,string arguments,string cwd) {
        Start(executable,arguments,cwd,null);
    }
    public void Start(string executable,string arguments,string cwd,Action<Action> creationBoundary) {
        if(launched||root!=IntPtr.Zero||job!=IntPtr.Zero||closed)throw new IOException("Capture already started/closed");
        IntPtr attributes=IntPtr.Zero,handles=IntPtr.Zero,jobs=IntPtr.Zero;
        bool attributesReady=false;
        string stage="create owned domain";
        try {
            job=CreateJobObjectW(IntPtr.Zero,null);Require(job!=IntPtr.Zero,"Create owned job");
            stage="create inherited pipes";
            var security=new Security{Size=Marshal.SizeOf(typeof(Security)),Inherit=1};
            Require(CreatePipe(out outRead,out outWrite,ref security,0),"Create stdout pipe");
            Require(SetHandleInformation(outRead,1,0),"Exclude stdout reader inheritance");
            Require(CreatePipe(out errRead,out errWrite,ref security,0),"Create stderr pipe");
            Require(SetHandleInformation(errRead,1,0),"Exclude stderr reader inheritance");
            input=CreateFileW("NUL",0x80000000,3,ref security,3,0,IntPtr.Zero);
            Require(input!=new IntPtr(-1),"Open inert stdin");
            IntPtr bytes=IntPtr.Zero;
            stage="prepare creation attributes";
            InitializeProcThreadAttributeList(IntPtr.Zero,2,0,ref bytes);
            if(bytes==IntPtr.Zero)throw new IOException("Missing attribute-list size");
            attributes=Marshal.AllocHGlobal(bytes);
            Require(InitializeProcThreadAttributeList(attributes,2,0,ref bytes),"Initialize owned attributes");attributesReady=true;
            handles=Marshal.AllocHGlobal(IntPtr.Size*3);
            Marshal.WriteIntPtr(handles,0,input);Marshal.WriteIntPtr(handles,IntPtr.Size,outWrite);Marshal.WriteIntPtr(handles,IntPtr.Size*2,errWrite);
            jobs=Marshal.AllocHGlobal(IntPtr.Size);Marshal.WriteIntPtr(jobs,job);
            Require(UpdateProcThreadAttribute(attributes,0,new IntPtr(0x20002),handles,new IntPtr(IntPtr.Size*3),IntPtr.Zero,IntPtr.Zero),"Bind inherited pipes");
            // JOB_LIST assigns before the child can run. Never launch first and
            // enumerate/enroll afterwards; unsupported hosts fail before launch.
            Require(UpdateProcThreadAttribute(attributes,0,new IntPtr(0x2000D),jobs,new IntPtr(IntPtr.Size),IntPtr.Zero,IntPtr.Zero),"Bind owned domain at creation");
            var startup=new StartupEx();startup.Info.Size=Marshal.SizeOf(typeof(StartupEx));startup.Info.Flags=0x100;
            startup.Info.Input=input;startup.Info.Output=outWrite;startup.Info.Error=errWrite;startup.Attributes=attributes;
            stage="create contained root";
            InvokeCreationBoundary(creationBoundary,()=>{
                ProcessInfo process;
                Require(CreateProcessW(executable,new StringBuilder("\""+executable+"\" "+arguments),IntPtr.Zero,IntPtr.Zero,true,0x08080000,IntPtr.Zero,cwd,ref startup,out process),"Create contained build child");
                // Retain native ownership before returning to an admission
                // callback that may itself fail after the creation call.
                root=process.Process;thread=process.Thread;Id=(int)process.Id;launched=true;
            });
            stage="read root identity";
            long created,exited,kernel,user;
            Require(GetProcessTimes(root,out created,out exited,out kernel,out user),"Read root creation identity");
            CreationUtc=DateTime.FromFileTimeUtc(created).ToString("o");
            stage="initialize stream pumps";
            // FileStream borrows each reader. CheckedClose remains its owner.
            outPipe=new FileStream(new SafeFileHandle(outRead,false),FileAccess.Read,4096,false);
            errPipe=new FileStream(new SafeFileHandle(errRead,false),FileAccess.Read,4096,false);
            outPump=outPipe.CopyToAsync(output);errPump=errPipe.CopyToAsync(error);
        } catch(Exception e) {
            LaunchFailure=new Failure(stage,e);throw;
        } finally {
            if(attributesReady)DeleteProcThreadAttributeList(attributes);
            foreach(IntPtr p in new[]{attributes,handles,jobs})if(p!=IntPtr.Zero)Marshal.FreeHGlobal(p);
            CloseStartup();
        }
    }
    public bool WaitForExit(int milliseconds) {
        if(!launched)return true;
        uint wait=WaitForSingleObject(root,(uint)milliseconds);
        if(wait==0xFFFFFFFF)throw new Win32Exception(Marshal.GetLastWin32Error(),"Wait exact root");
        return wait==0;
    }
    public uint ActiveProcesses() {
        if(job==IntPtr.Zero){if(!launched)return 0;throw new IOException("Missing owned domain");}
        Accounting data;Require(QueryInformationJobObject(job,1,out data,Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero),"Query owned domain");
        return data.Active;
    }
    public bool StdoutEof { get { return !launched || (outPump!=null && outPump.Status==TaskStatus.RanToCompletion); } }
    public bool StderrEof { get { return !launched || (errPump!=null && errPump.Status==TaskStatus.RanToCompletion); } }
    public bool PumpFailed { get { return (outPump!=null && (outPump.IsFaulted||outPump.IsCanceled)) || (errPump!=null && (errPump.IsFaulted||errPump.IsCanceled)); } }
    public int ExitCode {
        get { if(!launched||!WaitForExit(0))throw new IOException("Root exit unverified");uint code;Require(GetExitCodeProcess(root,out code),"Read root exit");return unchecked((int)code); }
    }
    public void FlushClosed() {
        if(!WaitForExit(0)||ActiveProcesses()!=0||!StdoutEof||!StderrEof)throw new IOException("Capture domain/EOF unverified");
        output.Flush(true);error.Flush(true);
    }
    public Proof ProveClosed() {
        FlushClosed();
        if(launched && String.IsNullOrEmpty(CreationUtc))throw new IOException("Missing root creation identity");
        if(CloseoutProof==null)CloseoutProof=new Proof(this);
        return CloseoutProof;
    }
    public byte[] LogBytes(bool stderr) {
        FlushClosed();var stream=stderr?error:output;
        if(stream.Length>Int32.MaxValue)throw new IOException("Capture receipt too large");
        var bytes=new byte[(int)stream.Length];stream.Position=0;int read=0;
        while(read<bytes.Length){int n=stream.Read(bytes,read,bytes.Length-read);if(n==0)throw new IOException("Incomplete capture bytes");read+=n;}
        return bytes;
    }
    public Task<Data> BeginSnapshot(long maximumBytes) {
        if(maximumBytes<1||maximumBytes>16777216)throw new IOException("Invalid bounded parent receipt size");
        if(snapshotWork!=null)return snapshotWork;
        // Check before allocating/read/encoding; oversized original logs remain
        // untouched in their retained context. This is a receipt memory ceiling,
        // not a promise that disk work fits the caller's wall-clock budget.
        snapshotWork=Task.Run(()=>{
            CheckReceiptBytes(output.Length,error.Length,maximumBytes);
            return new Data(ProveClosed(),LogBytes(false),LogBytes(true));
        });
        return snapshotWork;
    }
    public static void CheckReceiptBytes(long stdout,long stderr,long maximumBytes) {
        if(maximumBytes<1||maximumBytes>16777216||stdout<0||stderr<0||
            stdout>maximumBytes||stderr>maximumBytes-stdout)
            throw new IOException("Parent logs exceed bounded receipt memory ceiling");
    }
    public Task BeginDisposeClosed() {
        if(disposeWork==null||disposeWork.IsFaulted||disposeWork.IsCanceled)disposeWork=Task.Run((Action)DisposeClosed);
        return disposeWork;
    }
    public void DisposeClosed() {
        if(closed)return;
        if(CloseoutProof==null)throw new IOException("No completed capture snapshot");
        if(job!=IntPtr.Zero && ActiveProcesses()!=0)throw new IOException("Owned domain became active");
        string stage="dispose completed streams";
        try {
            if(outPipe!=null)outPipe.Dispose();if(errPipe!=null)errPipe.Dispose();output.Dispose();error.Dispose();
            stage="close startup handles";CloseStartup();
            stage="close stdout reader";Close(ref outRead);
            stage="close stderr reader";Close(ref errRead);
            stage="close root witness";Close(ref root);
            stage="close domain witness";Close(ref job);closed=true;
        } catch(Exception e) { CleanupFailure=new Failure(stage,e);throw; }
    }
    // No finalizer, Kill, breakaway, job termination or KILL_ON_JOB_CLOSE.
    // Unknown closeout keeps this object and domain/stream handles in its owner.
}
'@
    $captureHash=[Security.Cryptography.SHA256]::Create()
    try{$captureDigest=[BitConverter]::ToString($captureHash.ComputeHash([Text.Encoding]::UTF8.GetBytes($captureSource))).Replace('-','').ToLowerInvariant()}
    finally{$captureHash.Dispose()}
    if('PdBuildParentCapture' -as [type]){
        if([PdBuildParentCapture]::SourceSha256 -cne $captureDigest){throw 'Loaded parent capture type differs from current source; no native launch admitted.'}
        return
    }
    Add-Type -TypeDefinition ($captureSource.Replace('PD_BUILD_SOURCE_SHA256',$captureDigest))
}

function Assert-BuildParentCloseoutFields($Proof,[string]$RequestId,[string]$RunId,[int]$RootPid,[string]$CreationUtc,[string]$DomainId) {
    if($null -eq $Proof -or $Proof.Schema -cne 'pd2.build-parent-closeout.v1' -or
       $Proof.RequestId -cne $RequestId -or $Proof.RunId -cne $RunId -or
       $Proof.RootPid -ne $RootPid -or $Proof.CreationUtc -cne $CreationUtc -or
       $RootPid -lt 0 -or ($RootPid -gt 0 -and -not $CreationUtc) -or
       -not $DomainId -or $Proof.DomainId -cne $DomainId -or
       $Proof.CaptureSourceSha256 -notmatch '^[0-9a-f]{64}$' -or $Proof.WrapperPid -ne $PID -or
       $Proof.RootExited -isnot [bool] -or $Proof.RootExited -ne $true -or
       $Proof.DomainActive -isnot [ValueType] -or $Proof.DomainActive -is [bool] -or $Proof.DomainActive -ne 0 -or
       $Proof.StdoutEof -isnot [bool] -or $Proof.StdoutEof -ne $true -or
       $Proof.StderrEof -isnot [bool] -or $Proof.StderrEof -ne $true -or
       $Proof.Flushed -isnot [bool] -or $Proof.Flushed -ne $true){
        throw 'Retained build parent domain/EOF closeout unverified; retain own protocol locks.'
    }
}

function Get-BuildParentDiagnostics($Operation) {
    $capture=$Operation.Capture
    if($null -eq $Operation.LaunchFailure -and $null -ne $capture.LaunchFailure){$Operation.LaunchFailure=$capture.LaunchFailure}
    if($null -eq $Operation.CleanupFailure -and $null -ne $capture.CleanupFailure){$Operation.CleanupFailure=$capture.CleanupFailure}
    $launch=$null
    if($null -ne $Operation.LaunchError){
        $launch=@{}
        foreach($key in @('type','message','error_id','stage','native_error')){
            $source=$Operation.LaunchError
            $value=if($source -is [Collections.IDictionary]){$source[$key]}elseif($source.PSObject.Properties[$key]){$source.$key}else{$null}
            if($null -ne $value){$text=[string]$value;$launch[$key]=$text.Substring(0,[math]::Min(2048,$text.Length))}
        }
    }
    $outcome=if(-not $capture.Launched){'no_child'}elseif($null -ne $launch -or $null -ne $Operation.LaunchFailure){'failed_launch'}else{'exited_child'}
    return @{outcome=$outcome;launch_error=$launch;native_launch_failure=$Operation.LaunchFailure;
        native_cleanup_failure=$Operation.CleanupFailure}
}

function Start-BuildParentReceiptWorker($Capture,$Data,[string]$Name='', $Value=$null,$ExistingReceipt=$null) {
    if($null -ne $script:BuildRetentionReceiptWorker -and -not $script:BuildRetentionReceiptWorker.Complete){
        throw 'Previous parent receipt worker is still owned; no second publisher admitted.'
    }
    if(-not $Name){$Name='parent-closeout-'+$Capture.DomainId+'.json'}
    if($null -eq $Value){$Value=@{proof=$Data.Proof;logs=@{stdout=$Data.StdoutBase64;stderr=$Data.StderrBase64}}}
    $space=[Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
    $shell=[Management.Automation.PowerShell]::Create()
    $shell.Runspace=$space
    $work=[pscustomobject]@{Phase='opening';Complete=$false;Space=$space;Shell=$shell;Handle=$null;Result=$null;
        Context=$script:BuildRetentionContext;Policy=$script:SmokeStoragePolicy;Capture=$Capture;Data=$Data;
        Name=$Name;Value=$Value;ExistingReceipt=$ExistingReceipt;
        Library=(Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/smoke-verify/lib');SupervisorOperation=$null}
    $script:BuildRetentionReceiptWorker=$work
    $space.OpenAsync()
    return $work
}

function Read-BuildParentReceiptWorker($Work) {
    if($Work.Complete){return $Work.Result}
    if($Work.Phase -eq 'opening'){
        $state=$Work.Space.RunspaceStateInfo.State.ToString()
        if($state -in @('Broken','Closed')){throw 'Parent receipt runspace failed before publication.'}
        if($state -ne 'Opened'){return}
        $body=@'
param($library,$context,$policy,$name,$value,$existing,$ownerRecord)
$ErrorActionPreference='Stop'
. (Join-Path $library 'Storage-Harness.ps1')
. (Join-Path $library 'Runner-Retention.ps1')
$script:SmokeStoragePolicy=$policy
$result=if($null -eq $existing){Write-SmokeRunnerSupervisedJson -Context $context -Name $name -Value $value -OwnerRecord $ownerRecord}else{$existing}
$receiptContext=$result.SupervisorContext
$closeout=$result.SupervisorCloseout
if($result.ParentRunId -cne $context.RunId -or $null -eq $receiptContext -or $null -eq $closeout -or
   $closeout.RootExited -ne $true -or $closeout.DomainActive -ne 0 -or $closeout.HandlesClosed -ne $true -or
   $closeout.OutputMode -cne 'nul' -or $closeout.ExitCode -ne 0 -or
   $closeout.RootPid -ne $receiptContext.Owner.pid -or $closeout.CreationTicks -ne $receiptContext.Owner.creation_ticks){
    throw 'Parent supervisor lifetime/physical closeout unverified.'
}
$expected=[IO.Path]::GetFullPath((Join-Path $receiptContext.RunRoot $name))
if($result.Path -cne $expected -or $result.RunId -cne $receiptContext.RunId -or $result.AccountingDomain -cne 'archive'){
    throw 'Parent accounted receipt identity mismatch.'
}
Assert-SmokePlainTree -Path $expected
$stream=[IO.File]::Open($expected,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try{
    if($stream.Length -ne $result.Bytes -or $stream.Length -gt 33554432){throw 'Parent accounted receipt length mismatch.'}
    $hash=[Security.Cryptography.SHA256]::Create()
    try{$digest=[BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$hash.Dispose()}
    if($null -ne $existing -and $digest -cne $existing.ReceiptSha256){throw 'Parent immutable receipt changed.'}
    $stream.Position=0;$reader=New-Object IO.StreamReader($stream,[Text.Encoding]::UTF8,$true,4096,$true)
    try{$observed=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
    if((ConvertTo-Json $observed -Depth 50 -Compress) -cne (ConvertTo-Json $value -Depth 50 -Compress)){
        throw 'Parent accounted receipt content mismatch.'
    }
}finally{$stream.Dispose()}
if($null -eq $existing -and -not $result.PSObject.Properties.Match('ReceiptSha256').Count){$result|Add-Member -NotePropertyName ReceiptSha256 -NotePropertyValue $digest}
if($result.ReceiptSha256 -cne $digest){throw 'Parent supervisor receipt hash mismatch.'}
$result
'@
        [void]$Work.Shell.AddScript($body).AddArgument($Work.Library).AddArgument($Work.Context).AddArgument($Work.Policy).AddArgument($Work.Name).AddArgument($Work.Value).AddArgument($Work.ExistingReceipt).AddArgument($Work)
        $Work.Handle=$Work.Shell.BeginInvoke();$Work.Phase='publishing'
    }
    if($Work.Phase -eq 'publishing'){
        if(-not $Work.Handle.IsCompleted){return}
        $values=$Work.Shell.EndInvoke($Work.Handle)
        if($Work.Shell.HadErrors){throw $Work.Shell.Streams.Error[0].Exception}
        if($values.Count -ne 1 -or -not $values[0].Path){throw 'Parent publisher returned no unique accounted receipt.'}
        $Work.Result=$values[0];$Work.Space.CloseAsync();$Work.Phase='closing'
    }
    if($Work.Phase -eq 'closing'){
        if($Work.Space.RunspaceStateInfo.State.ToString() -ne 'Closed'){return}
        $Work.Shell.Dispose();$Work.Space.Dispose();$Work.Complete=$true
        return $Work.Result
    }
}

function Complete-BuildParentCapture($Capture,[int]$CloseoutMilliseconds=10000,[long]$MaximumLogReceiptBytes=16777216,$LaunchError=$null) {
    if($CloseoutMilliseconds -lt 1 -or $CloseoutMilliseconds -gt 30000){throw 'Invalid finite parent closeout budget.'}
    if($null -eq $script:BuildRetentionContext -or $script:BuildRetentionContext.Owner.pid -ne $PID){throw 'Parent capture context owner mismatch.'}
    if($MaximumLogReceiptBytes -lt 1 -or $MaximumLogReceiptBytes -gt 16777216){throw 'Invalid bounded parent receipt memory ceiling.'}
    $op=$script:BuildRetentionCloseoutOperation
    if($null -ne $op -and -not [object]::ReferenceEquals($op.Capture,$Capture)){
        if(-not $op.Complete){throw 'Another parent closeout operation is still owned; no capture replacement admitted.'}
        $op=$null
    }
    if($null -eq $op){
        $op=[pscustomobject]@{Capture=$Capture;Context=$script:BuildRetentionContext;OwnerPid=$PID;
            RequestId=$Capture.RequestId;RunId=$Capture.RunId;DomainId=$Capture.DomainId;RootPid=$Capture.Id;CreationUtc=$Capture.CreationUtc;MaximumBytes=$MaximumLogReceiptBytes;
            Snapshot=$null;Data=$null;Publication=$null;Receipt=$null;SnapshotVerification=$null;CompletionVerification=$null;Disposal=$null;Disposed=$false;
            CompletionWorker=$null;CompletionReceipt=$null;CompletionValue=$null;SnapshotValue=$null;Complete=$false;
            LaunchError=$LaunchError;LaunchFailure=$null;CleanupFailure=$null;LastFailure=$null}
        $script:BuildRetentionCloseoutOperation=$op
    }elseif(-not [object]::ReferenceEquals($op.Context,$script:BuildRetentionContext) -or
        $op.OwnerPid -ne $PID -or $op.RequestId -cne $Capture.RequestId -or $op.RunId -cne $Capture.RunId -or
        $op.DomainId -cne $Capture.DomainId -or $op.RootPid -ne $Capture.Id -or $op.CreationUtc -cne $Capture.CreationUtc -or $op.MaximumBytes -ne $MaximumLogReceiptBytes){
        throw 'Parent closeout continuation identity mismatch.'
    }
    if($op.RunId -cne $script:BuildRetentionContext.RunId){throw 'Parent closeout run mismatch.'}
    if($null -ne $LaunchError -and $null -eq $op.LaunchError){$op.LaunchError=$LaunchError}
    # A matching explicit call resumes the retained tasks. Late completion alone
    # never clears Unknown or creates a release record. No publisher is restarted.
    $verifyRequired=$null -ne $op.Receipt
    $completionVerifyRequired=$null -ne $op.CompletionWorker
    $deadline=[Diagnostics.Stopwatch]::StartNew();$failure=$null
    do{
        try{
            if(-not $op.Disposed -and $Capture.PumpFailed){throw 'Parent log pump failed; retain capture and protocol locks.'}
            if($null -eq $op.Snapshot -and $Capture.WaitForExit(0) -and $Capture.ActiveProcesses() -eq 0 -and $Capture.StdoutEof -and $Capture.StderrEof){
                $op.Snapshot=$Capture.BeginSnapshot($MaximumLogReceiptBytes)
            }
            if($null -ne $op.Snapshot -and $null -eq $op.Data -and $op.Snapshot.IsCompleted){
                if($op.Snapshot.IsCanceled){throw 'Parent capture snapshot cancelled; ownership unclosed.'}
                if($op.Snapshot.IsFaulted){throw $op.Snapshot.Exception.GetBaseException()}
                $op.Data=$op.Snapshot.GetAwaiter().GetResult()
                Assert-BuildParentCloseoutFields $op.Data.Proof $Capture.RequestId $Capture.RunId $Capture.Id $Capture.CreationUtc $Capture.DomainId
            }
            if($null -ne $op.Data -and $null -eq $op.Publication -and $deadline.ElapsedMilliseconds -lt $CloseoutMilliseconds){
                $op.SnapshotValue=@{proof=$op.Data.Proof;logs=@{stdout=$op.Data.StdoutBase64;stderr=$op.Data.StderrBase64};diagnostics=(Get-BuildParentDiagnostics $op)}
                $op.Publication=Start-BuildParentReceiptWorker $Capture $op.Data -Value $op.SnapshotValue
            }
            if($null -ne $op.Publication -and $null -eq $op.Receipt){$op.Receipt=Read-BuildParentReceiptWorker $op.Publication}
            if($null -ne $op.CompletionWorker -and $null -eq $op.CompletionReceipt){$op.CompletionReceipt=Read-BuildParentReceiptWorker $op.CompletionWorker}
            # Drain this operation's pending completion publisher before opening
            # any receipt-only verifier. Never replace a worker that is still live.
            if($null -ne $op.CompletionWorker -and -not $op.CompletionWorker.Complete){continue}
            # A prior completion verifier may still own the single runspace lane.
            # Drain it only as completion evidence, never as snapshot evidence.
            if($null -ne $op.CompletionVerification -and -not $op.CompletionVerification.Complete){
                if($null -eq (Read-BuildParentReceiptWorker $op.CompletionVerification)){continue}
            }
            if($verifyRequired -and $null -ne $op.Receipt){
                if($null -eq $op.SnapshotVerification){$op.SnapshotVerification=Start-BuildParentReceiptWorker $Capture $op.Data -Value $op.SnapshotValue -ExistingReceipt $op.Receipt}
                if($null -eq (Read-BuildParentReceiptWorker $op.SnapshotVerification)){continue}
                $verifyRequired=$false;$op.SnapshotVerification=$null
            }
            if($completionVerifyRequired -and $null -ne $op.CompletionReceipt){
                if($null -eq $op.CompletionVerification){$op.CompletionVerification=Start-BuildParentReceiptWorker $Capture $op.Data -Name ('parent-closeout-'+$Capture.DomainId+'-completion.json') -Value $op.CompletionValue -ExistingReceipt $op.CompletionReceipt}
                if($null -eq (Read-BuildParentReceiptWorker $op.CompletionVerification)){continue}
                $completionVerifyRequired=$false;$op.CompletionVerification=$null
            }
            if($null -ne $op.Receipt -and -not $op.Disposed -and $deadline.ElapsedMilliseconds -lt $CloseoutMilliseconds){
                if($null -eq $op.Disposal -or $op.Disposal.IsCanceled -or $op.Disposal.IsFaulted){$op.Disposal=$Capture.BeginDisposeClosed()}
                if($op.Disposal.IsCompleted){
                    if($op.Disposal.IsCanceled){throw 'Parent capture disposal cancelled; ownership unclosed.'}
                    if($op.Disposal.IsFaulted){throw $op.Disposal.Exception.GetBaseException()}
                    [void]$op.Disposal.GetAwaiter().GetResult();$op.Disposed=$true
                }
            }
            if($op.Disposed -and $null -eq $op.CompletionWorker -and $deadline.ElapsedMilliseconds -lt $CloseoutMilliseconds){
                $op.CompletionValue=@{schema='pd2.build-parent-completion.v1';proof=$op.Data.Proof;
                    snapshot_receipt=$op.Receipt;diagnostics=(Get-BuildParentDiagnostics $op);physical_disposal_complete=$true}
                $op.CompletionWorker=Start-BuildParentReceiptWorker $Capture $op.Data -Name ('parent-closeout-'+$Capture.DomainId+'-completion.json') -Value $op.CompletionValue
            }
            if($null -ne $op.CompletionWorker -and $null -eq $op.CompletionReceipt){$op.CompletionReceipt=Read-BuildParentReceiptWorker $op.CompletionWorker}
            if($null -ne $op.CompletionReceipt -and $deadline.ElapsedMilliseconds -lt $CloseoutMilliseconds){
                $op.Complete=$true;$script:BuildRetentionFailure=$null
                $script:BuildRetentionCloseout=@{Proof=$op.Data.Proof;Path=$op.Receipt.Path;CompletionPath=$op.CompletionReceipt.Path;Operation=$op}
                return $op.Data.Proof
            }
        }catch{$failure=$_.Exception.Message;break}
        if($deadline.ElapsedMilliseconds -lt $CloseoutMilliseconds){Start-Sleep -Milliseconds 1}
    }while($deadline.ElapsedMilliseconds -lt $CloseoutMilliseconds)
    if(-not $failure){$failure='Parent domain/EOF, flush, publication or disposal exceeded finite closeout deadline.'}
    # Do not call a possibly stuck publisher again to report its own timeout.
    # Keep the managed tasks/runspace, capture handles, original logs, active
    # protocol and accounted context. Their late completion is not queue release.
    $script:BuildRetentionFailure=@{
        schema='pd2.build-parent-incomplete.v1';request_id=$Capture.RequestId;run_id=$Capture.RunId;
        domain_id=$Capture.DomainId;root_pid=$Capture.Id;creation_utc=$Capture.CreationUtc;
        stdout=$Capture.StdoutPath;stderr=$Capture.StderrPath;verdict='unknown';reason=$failure;
        launch_error=$op.LaunchError;diagnostics=(Get-BuildParentDiagnostics $op);
        native_launch_failure=$op.LaunchFailure;native_cleanup_failure=$op.CleanupFailure;
        snapshot_task=$op.Snapshot;receipt_worker=$op.Publication;disposal_task=$op.Disposal;operation=$op;
        maximum_receipt_bytes=$MaximumLogReceiptBytes;no_termination_requested=$true}
    $op.LastFailure=$script:BuildRetentionFailure
    $short=$failure.Substring(0,[math]::Min(2048,$failure.Length))
    throw ($short+' Retained context '+$Capture.RunId+', domain '+$Capture.DomainId+'; capture/tasks/protocol remain owned.')
}

function Invoke-BuildRetainedCapturedChild([string[]]$ChildArgs,$QueueToken,[int]$TimeoutSeconds,[string]$BuildDir) {
    $stdoutLog=Get-BuildStdoutLogPath $BuildDir;$stderrLog=Get-BuildStderrLogPath $BuildDir
    foreach($path in @($stdoutLog,$stderrLog)){Assert-SmokePlainTree -Path $path}
    Initialize-BuildParentCaptureType
    $script:BuildRetentionCloseout=$null
    try{$capture=[PdBuildParentCapture]::new($stdoutLog,$stderrLog,$QueueToken.RequestId,$script:BuildRetentionContext.RunId)}
    catch{
        $script:BuildRetentionFailure=@{
            schema='pd2.build-parent-open-failure.v1';request_id=$QueueToken.RequestId;run_id=$script:BuildRetentionContext.RunId;
            stdout=$stdoutLog;stderr=$stderrLog;no_child_created=$true;collision_bytes_preserved=$true;reason=$_.Exception.Message}
        throw
    }
    $script:BuildRetentionCapture=$capture
    $failure=$null;$timedOut=$false
    try{
        $executable=(Get-Command powershell.exe -CommandType Application -ErrorAction Stop).Source
        $capture.Start($executable,($ChildArgs -join ' '),$ProjectDir)
        $script:BuildRetentionChild=$capture
        $lastProgress=Get-BuildProgressSignal $BuildDir;$lastProgressUtc=[DateTime]::UtcNow
        Update-BuildQueueActive $QueueToken $capture.Id $stdoutLog $stderrLog
        while(-not $capture.WaitForExit(2000)){
            Update-BuildQueueActive $QueueToken $capture.Id $stdoutLog $stderrLog
            $progress=Get-BuildProgressSignal $BuildDir
            if(Test-BuildProgressAdvanced $lastProgress $progress){$lastProgress=$progress;$lastProgressUtc=[DateTime]::UtcNow}
            if($TimeoutSeconds -gt 0 -and ([DateTime]::UtcNow-$lastProgressUtc).TotalSeconds -ge $TimeoutSeconds){
                $timedOut=$true;break
            }
        }
    }catch{
        $base=$_.Exception.GetBaseException()
        $failure=@{type=$base.GetType().FullName;message=$base.Message;error_id=$_.FullyQualifiedErrorId;
            native_error=if($base -is [ComponentModel.Win32Exception]){$base.NativeErrorCode}else{0}}
    }
    # Timeout does not grant PID-tree termination. If the owned domain remains,
    # bounded closeout fails and keeps evidence/locks. Ordinary watchdog unchanged.
    $proof=Complete-BuildParentCapture $capture -LaunchError $failure
    if($failure){throw ('Retained parent launch/capture failed: '+$failure.message)}
    Update-BuildQueueActive $QueueToken $capture.Id $stdoutLog $stderrLog
    if($timedOut){return $QueueTimeoutExitCode}
    return [int]$proof.ExitCode
}

function Assert-BuildRetentionOptions([bool]$Retained,[hashtable]$Modes,[string]$DevMods,[string]$Session) {
    if(-not $Retained){return}
    foreach($name in @('Clean','NoQueue','Remove','RemoveAll','SelfTest','Force','List')){
        if($Modes[$name]){throw "Retained build refuses $name before setup."}
    }
    if($DevMods){throw 'Retained build refuses DevMods before setup.'}
    if(-not $Session){throw 'Retained build requires an explicit stable Session.'}
}

function Assert-BuildRetainedChild([string]$Path) {
    Assert-SmokePlainTree -Path $Path
    $approved=$script:BuildRetentionReviewedChildSha256
    if(-not $approved -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $approved){
        throw 'Retained build child has no released, reviewed exact capability; no queue, output or native launch admitted.'
    }
}

function Assert-BuildProtocolPath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    $root=[IO.Path]::GetFullPath($SessionBuildRoot).TrimEnd('\')
    if(-not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Build protocol path escaped session root.'}
    Assert-SmokePlainTree -Path $full
    return $full
}

function Get-BuildProtocolBindingDirectory([string]$Path) {
    $full=Assert-BuildProtocolPath $Path
    $hash=[Security.Cryptography.SHA256]::Create()
    try {$id=[BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($full.ToLowerInvariant()))).Replace('-','').ToLowerInvariant()}
    finally {$hash.Dispose()}
    return Join-Path (Join-Path $QueueDir '.retained-bindings') $id
}

function Test-BuildProtocolRetained([string]$Path) {
    # A retained binding survives malformed JSON and later wrapper invocations.
    $binding=Get-BuildProtocolBindingDirectory $Path
    Assert-SmokePlainTree -Path $binding -Descendants
    return Test-Path -LiteralPath $binding
}

function Write-BuildProtocolReceipt([string]$Path,[string]$Phase,[byte[]]$Bytes) {
    if(-not $script:BuildRetentionContext){throw 'Build retention context missing.'}
    if($script:BuildRetentionContext.Owner.pid -ne $PID){throw 'Build receipt owner mismatch.'}
    $full=Assert-BuildProtocolPath $Path
    if($null -eq $Bytes){$Bytes=[byte[]]@()}
    $hash=[Security.Cryptography.SHA256]::Create()
    try {$digest=[BitConverter]::ToString($hash.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant()}
    finally {$hash.Dispose()}
    $value=@{schema='pd2.build-protocol-receipt.v1';path=$full;phase=$Phase;bytes=$Bytes.Length;
        sha256=$digest;raw_base64=[Convert]::ToBase64String($Bytes);
        run_id=$script:BuildRetentionContext.RunId;owner=$script:BuildRetentionContext.Owner}
    return Write-SmokeRunnerRetainedJson -Context $script:BuildRetentionContext -Name ('protocol-'+[guid]::NewGuid().ToString('N')+'.json') -Value $value
}

function New-BuildProtocolBinding([string]$Path) {
    $directory=Get-BuildProtocolBindingDirectory $Path
    Assert-SmokePlainTree -Path $directory -Descendants
    [void][IO.Directory]::CreateDirectory($directory)
    $destination=Join-Path $directory ($script:BuildRetentionContext.RunId+'.json')
    Assert-SmokePlainTree -Path $destination
    $value=@{schema='pd2.build-protocol-binding.v1';path=[IO.Path]::GetFullPath($Path);
        run_id=$script:BuildRetentionContext.RunId;owner=$script:BuildRetentionContext.Owner}
    $bytes=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $value -Depth 8 -Compress))
    if(Test-Path -LiteralPath $destination){
        if([Convert]::ToBase64String([IO.File]::ReadAllBytes($destination)) -cne [Convert]::ToBase64String($bytes)){throw 'Build retained binding collision.'}
        return
    }
    [void](Write-BuildProtocolReceipt -Path $Path -Phase 'binding' -Bytes $bytes)
    $stream=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)} finally {$stream.Dispose()}
}

function ConvertFrom-BuildRetainedDurationBytes([byte[]]$Bytes) {
    try {
        $text=[Text.Encoding]::UTF8.GetString($Bytes).TrimStart([char]0xfeff).Trim()
        if(-not $text -or $text[0] -notin @('[','{')){throw 'Unsupported history envelope.'}
        $parsed=$text|ConvertFrom-Json -ErrorAction Stop
        $rows=@($parsed)
        if($null -eq $parsed){
            if($text -match '^\[\s*\]$'){return}
            throw 'Null duration history.'
        }
        foreach($row in $rows){
            foreach($field in @('Session','Target','ExitCode','DurationSeconds','CompletedUtc')){
                if(-not $row.PSObject.Properties.Match($field).Count){throw ('Missing '+$field)}
            }
            if($row.Session -isnot [string] -or -not $row.Session -or
               $row.Target -notin @('client','updater','tests','settings-tests','probe','all') -or
               $row.ExitCode -isnot [ValueType] -or $row.ExitCode -is [bool] -or
               $row.DurationSeconds -isnot [ValueType] -or $row.DurationSeconds -is [bool] -or
               [double]::IsNaN([double]$row.DurationSeconds) -or [double]::IsInfinity([double]$row.DurationSeconds) -or
               [double]$row.DurationSeconds -lt 0){throw 'Unsupported duration record.'}
            [void][DateTime]::Parse([string]$row.CompletedUtc,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind)
        }
        return $rows
    } catch {throw ('Retained build refuses malformed or unsupported duration history: '+$_.Exception.Message)}
}

function Read-BuildRetainedDurationHistory {
    Assert-SmokePlainTree -Path $QueueDurationsPath
    if(-not (Test-Path -LiteralPath $QueueDurationsPath)){return}
    return ConvertFrom-BuildRetainedDurationBytes ([IO.File]::ReadAllBytes($QueueDurationsPath))
}

function Assert-BuildOwnedProtocolBytes([string]$Path,[byte[]]$Bytes,[string]$RequestId='') {
    if($Path -eq $QueueDurationsPath){[void](ConvertFrom-BuildRetainedDurationBytes $Bytes);return}
    $record=[Text.Encoding]::UTF8.GetString($Bytes).TrimStart([char]0xfeff)|ConvertFrom-Json
    if($record.WrapperPid -ne $PID -or $record.RetentionRunId -cne $script:BuildRetentionContext.RunId -or
       ($RequestId -and $record.RequestId -cne $RequestId)){throw 'Build protocol is not this invocation owned record.'}
}

function Write-BuildRetainedProtocol([string]$Path,$Value) {
    if($null -eq $script:BuildRetentionQueueLock -or -not $script:BuildRetentionQueueLock.CanWrite -or
        -not [string]::Equals($script:BuildRetentionQueueLock.Name,$QueueLockPath,[StringComparison]::OrdinalIgnoreCase)){
        throw 'Retained queue write requires the canonical queue state lock.'
    }
    $full=Assert-BuildProtocolPath $Path
    $request=([IO.Path]::GetDirectoryName($full) -eq $QueueDir -and $full.EndsWith('.request.json'))
    if($full -ne $QueueDurationsPath -and $full -ne $QueueActivePath -and -not $request){throw 'Unknown retained queue output.'}
    $bytes=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $Value -Depth 8))
    Assert-BuildOwnedProtocolBytes $full $bytes
    $exists=Test-Path -LiteralPath $full
    if($exists -and $full.EndsWith('.request.json')){throw 'Retained request output collision.'}
    [void](Write-BuildProtocolReceipt -Path $full -Phase 'write-intent' -Bytes $bytes)
    New-BuildProtocolBinding $full
    $mode=if($exists){[IO.FileMode]::Open}else{[IO.FileMode]::CreateNew}
    $stream=[IO.File]::Open($full,$mode,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {
        if($exists){
            $old=New-Object byte[] ([int]$stream.Length)
            if($stream.Read($old,0,$old.Length) -ne $old.Length){throw 'Incomplete old queue record read.'}
            $id=if($full -eq $QueueDurationsPath){''}else{[string]$Value.RequestId}
            Assert-BuildOwnedProtocolBytes $full $old $id
            [void](Write-BuildProtocolReceipt -Path $full -Phase 'before-replace' -Bytes $old)
        }
        $stream.Position=0;$stream.SetLength(0);$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)
    } finally {$stream.Dispose()}
}

function Save-BuildOwnedProtocolRelease([string]$Path,[string]$RequestId) {
    if($null -eq $script:BuildRetentionQueueLock -or -not $script:BuildRetentionQueueLock.CanWrite -or
        -not [string]::Equals($script:BuildRetentionQueueLock.Name,$QueueLockPath,[StringComparison]::OrdinalIgnoreCase)){
        throw 'Retained queue release requires the canonical queue state lock.'
    }
    $full=Assert-BuildProtocolPath $Path
    $stream=[IO.File]::Open($full,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
    try {
        $bytes=New-Object byte[] ([int]$stream.Length)
        if($stream.Read($bytes,0,$bytes.Length) -ne $bytes.Length){throw 'Incomplete release record read.'}
        Assert-BuildOwnedProtocolBytes $full $bytes $RequestId
        [void](Write-BuildProtocolReceipt -Path $full -Phase 'before-own-release' -Bytes $bytes)
    } finally {$stream.Dispose()}
}

function Save-BuildOwnedSessionLock($Lock) {
    if($Lock.RetentionRunId -cne $script:BuildRetentionContext.RunId -or $Lock.OwnerPid -ne $PID){throw 'Build lock owner mismatch.'}
    $stream=$Lock.Stream
    $stream.Flush($true);$position=$stream.Position;$stream.Position=0
    try {
        $bytes=New-Object byte[] ([int]$stream.Length)
        if($stream.Read($bytes,0,$bytes.Length) -ne $bytes.Length){throw 'Incomplete owned lock read.'}
        [void](Write-BuildProtocolReceipt -Path $Lock.Path -Phase 'before-own-lock-release' -Bytes $bytes)
    } finally {$stream.Position=$position}
}

function Assert-BuildRetainedCloseout($QueueToken) {
    if(-not $script:BuildRetentionContext -or $null -eq $QueueToken){return}
    if($null -ne $script:BuildRetentionFailure){throw 'Retained parent failure remains unclosed; retain own protocol locks.'}
    $active=Read-QueueJson $QueueActivePath
    if($null -eq $active -or $active.RequestId -cne $QueueToken.RequestId -or $active.WrapperPid -ne $PID){
        throw 'Retained build queue closeout unverified; retain own protocol locks.'
    }
    if([int]$active.ChildPid -gt 0 -or $null -ne $script:BuildRetentionCapture){
        $capture=$script:BuildRetentionCapture
        $saved=$script:BuildRetentionCloseout
        if($null -eq $capture -or $null -eq $saved -or -not $saved.Path -or -not $saved.CompletionPath -or
           -not [object]::ReferenceEquals($saved.Operation,$script:BuildRetentionCloseoutOperation) -or -not $saved.Operation.Complete -or
           -not [object]::ReferenceEquals($saved.Proof,$capture.CloseoutProof) -or
           ([int]$active.ChildPid -gt 0 -and $capture.Id -ne [int]$active.ChildPid)){
            throw 'Retained build child closeout unverified; retain own protocol locks.'
        }
        Assert-BuildParentCloseoutFields $saved.Proof $QueueToken.RequestId $script:BuildRetentionContext.RunId $capture.Id $capture.CreationUtc $capture.DomainId
        if($saved.Proof.CaptureSourceSha256 -cne [PdBuildParentCapture]::SourceSha256){throw 'Parent closeout capture source mismatch; retain own protocol locks.'}
        Assert-SmokePlainTree -Path $saved.Path
        if(-not (Test-Path -LiteralPath $saved.Path -PathType Leaf)){throw 'Parent closeout accounting receipt missing; retain own protocol locks.'}
        Assert-SmokePlainTree -Path $saved.CompletionPath
        if(-not (Test-Path -LiteralPath $saved.CompletionPath -PathType Leaf)){throw 'Parent completion accounting receipt missing; retain own protocol locks.'}
    }
}

function New-BuildRetentionInvocation([string]$BuildDir,[string]$SessionName) {
    $context=New-SmokeRunnerRetentionContext -ProjectRoot $ProjectDir
    $value=@{schema='pd2.build-retention-context.v1';run_id=$context.RunId;owner=$context.Owner;
        project_root=$ProjectDir;build_dir=$BuildDir;session=$SessionName;target=$Target;
        created_utc=[DateTime]::UtcNow.ToString('o');stdout=(Join-Path $context.RunRoot '_build-session.out.log');
        stderr=(Join-Path $context.RunRoot '_build-session.err.log');reviewed_child_sha256=$script:BuildRetentionReviewedChildSha256}
    $receipt=Write-SmokeRunnerRetainedJson -Context $context -Name 'build-context.json' -Value $value
    $context|Add-Member -NotePropertyName BuildContextPath -NotePropertyValue $receipt.Path
    return $context
}

function Show-BuildRetainedTail([string]$SessionName,[int]$Lines,[bool]$Follow) {
    $receipts=Join-Path $ProjectDir '.claude/smoke-storage/receipts'
    Assert-SmokePlainTree -Path $receipts -Descendants
    $matches=@(foreach($directory in @(Get-ChildItem -LiteralPath $receipts -Directory -Filter 'runner-*' -ErrorAction SilentlyContinue)){
        $path=Join-Path $directory.FullName 'build-context.json'
        if(Test-Path -LiteralPath $path){
            $record=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
            if($record.schema -cne 'pd2.build-retention-context.v1'){throw 'Invalid retained build context.'}
            if($record.session -ceq $SessionName -and $record.project_root -ceq $ProjectDir){
                foreach($leaf in @('stdout','stderr')){
                    $expected=Join-Path $directory.FullName ('_build-session.'+$(if($leaf -eq 'stdout'){'out'}else{'err'})+'.log')
                    if($record.$leaf -cne $expected){throw 'Retained build log path escaped owned receipt.'}
                }
                $record
            }
        }
    })
    $record=$matches|Sort-Object created_utc -Descending|Select-Object -First 1
    if($null -eq $record){throw 'No retained build log context for this session.'}
    foreach($path in @($record.stdout,$record.stderr)){
        Assert-SmokePlainTree -Path $path
        if(Test-Path -LiteralPath $path){
            if($Follow){Get-Content -LiteralPath $path -Tail $Lines -Wait}else{Get-Content -LiteralPath $path -Tail $Lines}
        }
    }
}
