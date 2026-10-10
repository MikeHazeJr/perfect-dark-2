# Exact launch identities for native smoke cancellation. No UI/input APIs.
function Initialize-SmokeProcessHandleReader {
    if ('PdSmokeProcessHandleIdentity' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
public static class PdSmokeProcessHandleIdentity {
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint GetProcessId(IntPtr handle);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool QueryFullProcessImageNameW(IntPtr handle, uint flags, StringBuilder path, ref uint length);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessTimes(IntPtr handle, out long creation, out long exit, out long kernel, out long user);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateProcess(IntPtr handle, uint exitCode);
    public sealed class Identity { public int ProcessId; public string ExecutablePath; public string CreationUtc; public bool TerminationRequested; }
    static Identity ReadBasic(IntPtr handle, int expectedPid) {
        if(handle==IntPtr.Zero || handle==new IntPtr(-1)) throw new IOException("Invalid retained process handle");
        uint pid=GetProcessId(handle);
        if(pid==0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Process handle PID query failed");
        if(expectedPid<=0 || pid!=(uint)expectedPid) throw new IOException("Process handle PID mismatch");
        long c,e,k,u;
        if(!GetProcessTimes(handle,out c,out e,out k,out u) || c<=0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Process handle creation query failed");
        return new Identity { ProcessId=(int)pid, CreationUtc=DateTime.FromFileTimeUtc(c).ToString("o") };
    }
    public static Identity ReadNative(IntPtr handle, int expectedPid) {
        Identity value=ReadBasic(handle,expectedPid);
        StringBuilder path=new StringBuilder(32768); uint length=(uint)path.Capacity;
        if(!QueryFullProcessImageNameW(handle,0,path,ref length) || length==0 || length>=path.Capacity)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Process handle executable query failed");
        value.ExecutablePath=path.ToString();
        if(String.IsNullOrWhiteSpace(value.ExecutablePath)) throw new IOException("Missing process handle executable path");
        return value;
    }
    public static Identity Read(Process process) {
        var handle=process.SafeHandle; bool held=false;
        try {
            handle.DangerousAddRef(ref held);
            if(process.HasExited) throw new IOException("Launched process has exited");
            Identity result=ReadNative(handle.DangerousGetHandle(),process.Id);
            if(process.HasExited) throw new IOException("Launched process has exited");
            return result;
        } finally { if(held)handle.DangerousRelease(); }
    }
    // Only callers holding the Process from their own launch may use this
    // backstop. It never reacquires a process by PID or supplies authored identity.
    public static Identity CloseRetained(Process process, int waitMilliseconds) {
        var handle=process.SafeHandle; bool held=false;
        try {
            handle.DangerousAddRef(ref held);
            IntPtr raw=handle.DangerousGetHandle();
            Identity result=ReadBasic(raw,process.Id);
            if(!process.HasExited) {
                if(!TerminateProcess(raw,1) && !process.HasExited)
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Retained process termination failed");
                result.TerminationRequested=true;
            }
            if(!process.WaitForExit(waitMilliseconds) || !process.HasExited)
                throw new IOException("Retained process did not exit before closeout deadline");
            return result;
        } finally { if(held)handle.DangerousRelease(); }
    }
}
'@
}

function Get-SmokeProcessHandleIdentity {
    param([Parameter(Mandatory)][Diagnostics.Process]$Process)
    Initialize-SmokeProcessHandleReader
    return [PdSmokeProcessHandleIdentity]::Read($Process)
}

function Stop-SmokeRetainedProcess {
    param([Parameter(Mandatory)][Diagnostics.Process]$Process,
          [ValidateRange(1,30000)][int]$WaitMilliseconds=5000)
    Initialize-SmokeProcessHandleReader
    $identity=[PdSmokeProcessHandleIdentity]::CloseRetained($Process,$WaitMilliseconds)
    # Query failure retains the launch/lease. PID reuse does not select the new
    # process: the retained handle has already proven this launch exited.
    $current=Get-CimInstance Win32_Process -Filter ("ProcessId = {0}" -f $identity.ProcessId) -ErrorAction Stop
    if($current) {
        if(!$current.CreationDate -or [math]::Abs(((ConvertTo-SmokeProcessUtc $current.CreationDate)-
                (ConvertTo-SmokeProcessUtc $identity.CreationUtc)).TotalMilliseconds) -le 1) {
            throw 'Retained process absence unverified; retain resource leases'
        }
    }
    return [pscustomobject]@{status='exited';process_id=$identity.ProcessId;creation_utc=$identity.CreationUtc;
        termination_requested=$identity.TerminationRequested;verified_utc=[DateTime]::UtcNow.ToString('o')}
}

function ConvertTo-SmokeProcessPath {
    param([Parameter(Mandatory)][string]$Path)
    $windowsPath = $Path.Replace('/', '\')
    if (-not [IO.Path]::IsPathRooted($windowsPath)) { throw 'Process path must be absolute' }
    return [IO.Path]::GetFullPath($windowsPath)
}

function ConvertTo-SmokeProcessUtc {
    param([Parameter(Mandatory)]$Value)
    if ($Value -is [DateTimeOffset]) { return $Value.ToUniversalTime() }
    if ($Value -is [DateTime]) { return [DateTimeOffset]$Value.ToUniversalTime() }
    if ([string]$Value -notmatch '(Z|[+-]\d{2}:\d{2})$') {
        throw 'Serialized process time must include an explicit UTC offset'
    }
    return [DateTimeOffset]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture).ToUniversalTime()
}

function New-SmokeProcessOwnership {
    param(
        [Parameter(Mandatory)][Diagnostics.Process]$Process,
        [Parameter(Mandatory)][string]$ExpectedExecutable,
        [Parameter(Mandatory)][string]$CommandLineToken,
        [int]$ParentProcessId = $PID
    )
    # The actual Process returned by Start supplies identity, rather than a
    # later time-window guess or a process-name sweep.
    $identity = Get-SmokeProcessHandleIdentity -Process $Process
    $expected = ConvertTo-SmokeProcessPath $ExpectedExecutable
    if (-not [string]::Equals((ConvertTo-SmokeProcessPath $identity.ExecutablePath), $expected,
            [StringComparison]::OrdinalIgnoreCase)) { throw 'Launched executable identity mismatch' }
    return [pscustomobject]@{
        schema = 1
        process_id = $identity.ProcessId
        executable_path = $expected
        creation_utc = $identity.CreationUtc
        parent_process_id = $ParentProcessId
        command_line_token = $CommandLineToken
    }
}

function Write-SmokeProcessOwnership {
    param([Parameter(Mandatory)]$Ownership, [Parameter(Mandatory)][string]$InstallDir)
    $directory = Join-Path $InstallDir 'logs/smoke-process-ownership'
    $marker = Join-Path $InstallDir '.pd-storage-owner.json'
    if (Test-Path -LiteralPath $marker) {
        $installOwner = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
        $Ownership | Add-Member -NotePropertyName storage_install_id -NotePropertyValue $installOwner.id -Force
    }
    [void][IO.Directory]::CreateDirectory($directory)
    $path = Join-Path $directory ("owned-{0}.json" -f $Ownership.process_id)
    [IO.File]::WriteAllText($path, ($Ownership | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
    return $path
}

function Test-SmokeProcessOwnership {
    param([Parameter(Mandatory)]$Ownership, [Parameter(Mandatory)]$ProcessInfo)
    foreach ($property in @('ProcessId', 'ExecutablePath', 'CreationDate', 'ParentProcessId', 'CommandLine')) {
        if ($null -eq $ProcessInfo.PSObject.Properties[$property] -or
                $null -eq $ProcessInfo.$property -or [string]$ProcessInfo.$property -eq '') {
            return [pscustomobject]@{ status = 'unknown'; reason = "Missing process metadata: $property" }
        }
    }
    $reason = $null
    if ([int]$ProcessInfo.ProcessId -ne [int]$Ownership.process_id) { $reason = 'PID mismatch' }
    elseif (-not [string]::Equals((ConvertTo-SmokeProcessPath $ProcessInfo.ExecutablePath),
            (ConvertTo-SmokeProcessPath $Ownership.executable_path), [StringComparison]::OrdinalIgnoreCase)) {
        $reason = 'Executable mismatch'
    } elseif ([int]$ProcessInfo.ParentProcessId -ne [int]$Ownership.parent_process_id) { $reason = 'Parent mismatch' }
    elseif ([math]::Abs(((ConvertTo-SmokeProcessUtc $ProcessInfo.CreationDate) -
            (ConvertTo-SmokeProcessUtc $Ownership.creation_utc)).TotalMilliseconds) -gt 1) { $reason = 'Creation time mismatch (PID reuse)' }
    elseif (([string]$ProcessInfo.CommandLine).Replace('/', '\').IndexOf(
            ([string]$Ownership.command_line_token).Replace('/', '\'), [StringComparison]::OrdinalIgnoreCase) -lt 0) {
        $reason = 'Fixture/command token mismatch'
    }
    if ($reason) { return [pscustomobject]@{ status = 'mismatch'; reason = $reason } }
    return [pscustomobject]@{ status = 'matched'; reason = $null }
}

function Get-SmokeOwnedProcessState {
    param([Parameter(Mandatory)]$Ownership)
    if ($Ownership.schema -ne 1 -or [int]$Ownership.process_id -le 0 -or
            [int]$Ownership.process_id -eq $PID -or -not $Ownership.command_line_token) {
        throw 'Invalid or self-targeted process ownership record'
    }
    # Query failure is unknown, never an empty/zero-process result.
    $processInfo = Get-CimInstance Win32_Process -Filter ("ProcessId = {0}" -f [int]$Ownership.process_id) -ErrorAction Stop
    if ($null -eq $processInfo) { return [pscustomobject]@{ status = 'absent'; reason = $null } }
    $state = Test-SmokeProcessOwnership -Ownership $Ownership -ProcessInfo $processInfo
    if ($state.status -ne 'matched') { throw "Owned process cannot be verified: $($state.reason)" }
    return $state
}

function Stop-SmokeOwnedProcess {
    param([Parameter(Mandatory)]$Ownership, [ValidateRange(1, 30000)][int]$WaitMilliseconds = 10000)
    $state = Get-SmokeOwnedProcessState -Ownership $Ownership
    $requested = $false
    if ($state.status -eq 'matched') {
        $process = $null
        try {
            try { $process = Get-Process -Id ([int]$Ownership.process_id) -ErrorAction Stop }
            catch {
                if ((Get-SmokeOwnedProcessState $Ownership).status -ne 'absent') { throw }
            }
            if ($process) {
                # Open the specific process handle, then recheck identity before
                # terminating it. A recycled PID must not select another client.
                $identity = Get-SmokeProcessHandleIdentity -Process $process
                if (-not [string]::Equals((ConvertTo-SmokeProcessPath $identity.ExecutablePath),
                        (ConvertTo-SmokeProcessPath $Ownership.executable_path), [StringComparison]::OrdinalIgnoreCase) -or
                        [math]::Abs(((ConvertTo-SmokeProcessUtc $identity.CreationUtc) -
                        (ConvertTo-SmokeProcessUtc $Ownership.creation_utc)).TotalMilliseconds) -gt 1) {
                    throw 'Process handle no longer matches launch identity'
                }
                $closeout=Stop-SmokeRetainedProcess -Process $process -WaitMilliseconds $WaitMilliseconds
                $requested=$closeout.termination_requested
            }
        } finally { if ($process) { $process.Dispose() } }
    }
    if ((Get-SmokeOwnedProcessState $Ownership).status -ne 'absent') { throw 'Owned process remains; retain resource leases' }
    return [pscustomobject]@{
        status = 'exited'
        process_id = [int]$Ownership.process_id
        termination_requested = $requested
        verified_utc = [DateTime]::UtcNow.ToString('o')
    }
}
