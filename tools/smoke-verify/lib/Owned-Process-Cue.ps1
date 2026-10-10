# Optional retained-profile launch notification. This is ownership, not readiness.
# Requires the existing Owned-Process.ps1 and Reuse-Runtime.ps1 helpers.
function ConvertTo-SmokeCuePath {
    param([Parameter(Mandatory)][string]$Path)
    if ($Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.Substring(2).Contains(':') -or
            $Path -match '(^|[\\/])[^\\/]*[. ]([\\/]|$)') { throw 'Cue path alias rejected' }
    $canonical = [IO.Path]::GetFullPath($Path)
    if ($canonical -ne $Path.Replace('/', '\')) { throw 'Cue path alias rejected' }
    return $canonical
}

function Initialize-SmokeCueDirectoryType {
    if ('PdSmokeCueDirectory' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
public sealed class PdSmokeCueDirectory : IDisposable {
 [StructLayout(LayoutKind.Sequential)] struct Info { public uint attrs; public System.Runtime.InteropServices.ComTypes.FILETIME c,a,w; public uint volume,high,low,links,indexHigh,indexLow; }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string p,uint access,uint share,IntPtr security,uint disposition,uint flags,IntPtr template);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateDirectoryW(string p,IntPtr security);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info i);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFinalPathNameByHandleW(SafeFileHandle h,StringBuilder b,uint n,uint f);
 readonly List<SafeFileHandle> handles=new List<SafeFileHandle>();
 public PdSmokeCueDirectory(string path,bool fresh) {
  try {
   string root=System.IO.Path.GetPathRoot(path), current=root;
   Lock(current);
   string[] parts=path.Substring(root.Length).Split('\\');
   for(int x=0;x<parts.Length;x++) {
    current=System.IO.Path.Combine(current,parts[x]);
    bool leaf=x==parts.Length-1;
    if(fresh && (leaf || !Directory.Exists(current))) {
     if(!CreateDirectoryW(current,IntPtr.Zero))throw new IOException("Cue destination must be fresh; directory creation refused");
    }
    Lock(current);
   }
  } catch { Dispose(); throw; }
 }
 void Lock(string path) {
  SafeFileHandle h=CreateFileW(path,0x80u,3u,IntPtr.Zero,3,0x02200000u,IntPtr.Zero);
  if(h.IsInvalid){h.Dispose();throw new IOException("Cue directory identity unavailable");}
  Info i; StringBuilder b=new StringBuilder(32768);uint n=GetFinalPathNameByHandleW(h,b,(uint)b.Capacity,0);
  string actual=b.ToString();if(actual.StartsWith("\\\\?\\"))actual=actual.Substring(4);
  if(!GetFileInformationByHandle(h,out i)||(i.attrs&0x400)!=0||(i.attrs&16)==0||n==0||n>=b.Capacity||!String.Equals(actual.TrimEnd('\\'),path.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase)){h.Dispose();throw new IOException("Cue directory alias/reparse rejected");}
  handles.Add(h);
 }
 public void Dispose(){foreach(var h in handles)h.Dispose();handles.Clear();}
}
'@
}

function Write-SmokeCueJson {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$Value)
    # Neither an old cue nor a partial write may be observed as a fresh cue.
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.pending'
    $bytes = (New-Object Text.UTF8Encoding($false)).GetBytes(($Value | ConvertTo-Json -Depth 12))
    $stream = New-Object IO.FileStream($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    [IO.File]::Move($temporary, $Path) # Refuses existing output; failed pending bytes are retained.
}

function Read-SmokeCueJson {
    param([Parameter(Mandatory)][string]$Path)
    # The existing file/ancestor lock rejects hardlinks and reparse aliases.
    $lock = New-Object PdReuseConsumerLock((ConvertTo-SmokeCuePath $Path))
    try {
        if ($lock.Stream.Length -gt 65536) { throw 'Cue JSON exceeded bounded size' }
        $reader = New-Object IO.StreamReader($lock.Stream, (New-Object Text.UTF8Encoding($false, $true)))
        try { return ($reader.ReadToEnd() | ConvertFrom-Json) } finally { $reader.Dispose() }
    } finally { $lock.Dispose() }
}

function Open-SmokeOwnedProcessCueContext {
    param([Parameter(Mandatory)][string]$ProjectRoot, [Parameter(Mandatory)][string]$Directory,
          [Parameter(Mandatory)][string]$SessionId, [Parameter(Mandatory)][string]$ExpiresUtc,
          [Parameter(Mandatory)][int]$TimeoutSeconds)
    if ($SessionId -notmatch '^[A-Za-z0-9_-]{1,80}$') { throw 'Cue requires an explicit session identity' }
    $path = ConvertTo-SmokeCuePath $Directory
    $prefix = (ConvertTo-SmokeCuePath $ProjectRoot).TrimEnd('\') + '\.claude\smoke-process-cues\' + $SessionId + '\'
    if (-not $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
            $path.Substring($prefix.Length) -notmatch '^[A-Za-z0-9_-]{1,100}$') { throw 'Cue destination must be a fresh session-owned evidence directory' }
    $now = [DateTimeOffset]::UtcNow
    $expires = ConvertTo-SmokeProcessUtc $ExpiresUtc
    if ($TimeoutSeconds -le 0 -or $expires -le $now.AddSeconds($TimeoutSeconds) -or
            $expires -gt $now.AddMinutes(15)) { throw 'Cue context expired or outside bounded run window' }
    Initialize-SmokeCueDirectoryType
    $guard = New-Object PdSmokeCueDirectory($path, $true)
    try {
        $context = [pscustomobject]@{schema='pd2.owned-process-context.v1'; run_id=[Guid]::NewGuid().ToString('N');
            session_id=$SessionId; evidence_directory=$path; started_utc=$now.ToString('o'); expires_utc=$expires.ToString('o');
            runner_process_id=$PID; runner_creation_utc=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')}
        Write-SmokeCueJson -Path (Join-Path $path 'context.json') -Value $context
        return [pscustomobject]@{Directory=$path; Identity=$context; Guard=$guard}
    } catch { $guard.Dispose(); throw }
}

function Assert-SmokeCueContext {
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)]$ExpectedContext)
    if ($Context.schema -ne 'pd2.owned-process-context.v1') { throw 'Cue context schema mismatch' }
    foreach ($field in @('run_id','session_id','evidence_directory','runner_process_id')) {
        if ([string]$Context.$field -cne [string]$ExpectedContext.$field) { throw "Cue context mismatch: $field" }
    }
    # PS7 deserializes ISO dates as DateTime; PS5.1 leaves them as strings.
    foreach ($field in @('started_utc','expires_utc','runner_creation_utc')) {
        if ((ConvertTo-SmokeProcessUtc $Context.$field).UtcDateTime.Ticks -ne
                (ConvertTo-SmokeProcessUtc $ExpectedContext.$field).UtcDateTime.Ticks) { throw "Cue context mismatch: $field" }
    }
    $now = [DateTimeOffset]::UtcNow
    $start = ConvertTo-SmokeProcessUtc $Context.started_utc
    $end = ConvertTo-SmokeProcessUtc $Context.expires_utc
    if ($start -gt $now -or $end -le $now -or ($end-$start).TotalSeconds -gt 900) { throw 'Cue context expired or outside bounded run window' }
    $runner = Get-CimInstance Win32_Process -Filter ('ProcessId = {0}' -f [int]$Context.runner_process_id) -ErrorAction Stop
    if (!$runner -or !$runner.CreationDate -or [math]::Abs(((ConvertTo-SmokeProcessUtc $runner.CreationDate)-
            (ConvertTo-SmokeProcessUtc $Context.runner_creation_utc)).TotalMilliseconds) -gt 1) { throw 'Cue runner identity absent or reused' }
}

function Assert-SmokeCueBinding {
    param([Parameter(Mandatory)]$Binding, [Parameter(Mandatory)]$Context)
    foreach ($field in @('consumer_sha256','fixture_sha256','seed_id','base_binary_sha256')) {
        if ([string]$Binding.$field -cnotmatch '^[a-f0-9]{64}$') { throw "Cue binding hash invalid: $field" }
    }
    foreach ($field in @('consumer_path','fixture_path','base_directory','runtime_root','profile_directory')) {
        [void](ConvertTo-SmokeCuePath ([string]$Binding.$field))
    }
    if (!$Binding.base_install_id -or !$Binding.reuse_id -or !$Binding.fixture_name -or
            $Binding.profile_directory -cne (Join-Path $Binding.runtime_root 'profile') -or
            $Binding.runtime_root.StartsWith($Binding.base_directory.TrimEnd('\')+'\', [StringComparison]::OrdinalIgnoreCase) -or
            $Binding.base_directory -eq $Binding.runtime_root -or
            $Binding.consumer_path.StartsWith($Binding.base_directory.TrimEnd('\')+'\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Cue base, consumer or private profile identity mismatch'
    }
    $canonicalFixture = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) 'tests/menu_virtual_controller_agent_cancel.json'))
    if ($Binding.fixture_path -cne $canonicalFixture -or $Binding.fixture_name -cne 'menu_virtual_controller_agent_cancel') { throw 'Cue canonical fixture identity mismatch' }
    $marker = Read-SmokeCueJson (Join-Path $Binding.runtime_root '.pd-reuse-owner.json')
    if ($marker.id -cne $Binding.reuse_id -or $marker.base_id -cne $Binding.base_install_id -or
            [int]$marker.owner.pid -ne [int]$Context.runner_process_id -or
            [long]$marker.owner.creation_ticks -ne (ConvertTo-SmokeProcessUtc $Context.runner_creation_utc).UtcDateTime.ToFileTimeUtc()) { throw 'Cue profile lease identity mismatch' }
}

function Publish-SmokeOwnedProcessCue {
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)]$Ownership,
          [Parameter(Mandatory)]$Binding, [Parameter(Mandatory)]$ConsumerLock,
          [Parameter(Mandatory)]$FixtureLock)
    Assert-SmokeCueContext -Context $Context.Identity -ExpectedContext $Context.Identity
    foreach ($closed in @('cancelled.json','closed.json')) {
        if (Test-Path -LiteralPath (Join-Path $Context.Directory $closed)) { throw 'Cue context cancelled or closed' }
    }
    Assert-SmokeCueBinding $Binding $Context.Identity
    if ($ConsumerLock.Path -cne $Binding.consumer_path -or $ConsumerLock.Hash() -cne $Binding.consumer_sha256 -or
            $FixtureLock.Path -cne $Binding.fixture_path -or $FixtureLock.Hash() -cne $Binding.fixture_sha256 -or
            $Ownership.executable_path -cne $Binding.consumer_path -or
            (ConvertTo-SmokeCuePath $Ownership.command_line_token) -cne $Binding.fixture_path -or
            [int]$Ownership.parent_process_id -ne [int]$Context.Identity.runner_process_id) { throw 'Cue locked consumer, fixture or actor identity mismatch' }
    if ((Get-SmokeOwnedProcessState $Ownership).status -ne 'matched') { throw 'Cue actor exited before publication' }
    $cue = [pscustomobject]@{schema='pd2.owned-process-cue.v1';meaning='owned_process_only';
        published_utc=[DateTimeOffset]::UtcNow.ToString('o');context=$Context.Identity;binding=$Binding;actor=$Ownership}
    $path = Join-Path $Context.Directory 'owned-process.json'
    Write-SmokeCueJson -Path $path -Value $cue
    return $path
}

function Read-SmokeOwnedProcessCue {
    # Supply the expected context and binding from the authorized run request.
    # Call immediately before each use; never use cached publication validity.
    param([Parameter(Mandatory)][string]$Directory, [Parameter(Mandatory)]$ExpectedContext,
          [Parameter(Mandatory)]$ExpectedBinding)
    $path = ConvertTo-SmokeCuePath $Directory
    Initialize-SmokeCueDirectoryType
    $guard = New-Object PdSmokeCueDirectory($path, $false)
    $locks = @()
    try {
        if ($path -cne $ExpectedContext.evidence_directory) { throw 'Cue evidence destination mismatch' }
        $locks += Open-SmokeReuseConsumer -Path $ExpectedBinding.consumer_path -Sha256 $ExpectedBinding.consumer_sha256
        $locks += Open-SmokeReuseConsumer -Path $ExpectedBinding.fixture_path -Sha256 $ExpectedBinding.fixture_sha256
        foreach ($closed in @('cancelled.json','closed.json')) {
            if (Test-Path -LiteralPath (Join-Path $path $closed)) { throw 'Cue context cancelled or closed' }
        }
        $context = Read-SmokeCueJson (Join-Path $path 'context.json')
        Assert-SmokeCueContext $context $ExpectedContext
        $cue = Read-SmokeCueJson (Join-Path $path 'owned-process.json')
        if ($cue.schema -ne 'pd2.owned-process-cue.v1' -or $cue.meaning -ne 'owned_process_only') { throw 'Cue schema or meaning mismatch' }
        Assert-SmokeCueContext $cue.context $ExpectedContext
        Assert-SmokeCueBinding $ExpectedBinding $context
        foreach ($field in @('consumer_path','consumer_sha256','fixture_path','fixture_sha256','fixture_name',
                'base_install_id','seed_id','base_binary_sha256','base_directory','runtime_root','profile_directory','reuse_id')) {
            if ([string]$cue.binding.$field -cne [string]$ExpectedBinding.$field) { throw "Cue binding mismatch: $field" }
        }
        $locks += Open-SmokeReuseConsumer -Path (Join-Path $ExpectedBinding.base_directory 'PerfectDark.exe') -Sha256 $ExpectedBinding.base_binary_sha256
        if ($cue.actor.executable_path -cne $ExpectedBinding.consumer_path -or
                (ConvertTo-SmokeCuePath $cue.actor.command_line_token) -cne $ExpectedBinding.fixture_path -or
                [int]$cue.actor.parent_process_id -ne [int]$ExpectedContext.runner_process_id) { throw 'Cue actor binding mismatch' }
        if ((Get-SmokeOwnedProcessState $cue.actor).status -ne 'matched') { throw 'Cue actor exited before use' }
        # Recheck the finite context and cancellation after the slower hash checks.
        Assert-SmokeCueContext $context $ExpectedContext
        foreach ($closed in @('cancelled.json','closed.json')) {
            if (Test-Path -LiteralPath (Join-Path $path $closed)) { throw 'Cue context cancelled or closed' }
        }
        if ((Get-SmokeOwnedProcessState $cue.actor).status -ne 'matched') { throw 'Cue actor exited before use' }
        return $cue
    } finally { foreach ($lock in $locks) { $lock.Dispose() }; $guard.Dispose() }
}

function Close-SmokeOwnedProcessCueContext {
    param([Parameter(Mandatory)]$Context, [switch]$Cancelled)
    $leaf = if ($Cancelled) { 'cancelled.json' } else { 'closed.json' }
    Write-SmokeCueJson -Path (Join-Path $Context.Directory $leaf) -Value @{
        run_id=$Context.Identity.run_id; session_id=$Context.Identity.session_id;
        ended_utc=[DateTimeOffset]::UtcNow.ToString('o'); cancelled=[bool]$Cancelled}
}
# End of owned-process cue helpers.
