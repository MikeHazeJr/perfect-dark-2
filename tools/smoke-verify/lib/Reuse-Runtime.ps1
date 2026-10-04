# Retained-profile adapter. No seed/copy/reset operation is permitted here.
function Assert-SmokeReuseDefinition {
    param([Parameter(Mandatory)]$Test)
    $d = $Test.Definition
    foreach ($p in $d.PSObject.Properties) {
        if ($p.Name -in @('processes','remove_paths','fixtures','packed_fixtures','fault_processes','friend_identity_role','required_install_layout','target','runtime_strategy') -or $p.Name -like '*_fixtures') {
            throw "Reuse fixture mutation/orchestration rejected: $($p.Name)"
        }
    }
    if (($d.PSObject.Properties.Match('target').Count -and $d.target -ne 'pd') -or
        ($d.PSObject.Properties.Match('runtime_strategy').Count -and $d.runtime_strategy -ne 'harness')) { throw 'Reuse requires single-process pd harness.' }
    if ($Test.Name -ne 'menu_virtual_controller_agent_cancel') { throw 'Reuse fixture has not been vetted.' }
    foreach ($a in @($d.boot_args)) {
        if ($a -notin @('--no-update-check','--no-sound','--no-net','--portable','--main-menu')) { throw "Reuse boot argument rejected: $a" }
    }
    foreach ($required in @('--no-update-check','--no-net','--main-menu')) { if ($required -notin @($d.boot_args)) { throw "Reuse fixture missing $required" } }
    foreach ($ev in @($d.input_sequence)) {
        if ($ev.type -notin @('wait_until','controller_attach','controller_button','controller_detach','screenshot','exit')) { throw 'Reuse event type not vetted.' }
        if ($ev.type -eq 'screenshot' -and ([string]$ev.path -notmatch '^[A-Za-z0-9_-]+\.bmp$')) { throw 'Reuse screenshot path override rejected.' }
    }
    foreach ($artifact in @($d.retain_artifacts)) {
        if ([string]$artifact.src -notmatch '^[A-Za-z0-9_-]+\.bmp$' -or [string]$artifact.name -ne [string]$artifact.src) { throw 'Reuse artifact path override rejected.' }
    }
    $canonical = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) 'tests/menu_virtual_controller_agent_cancel.json'))
    if (-not $Test.PSObject.Properties.Match('Path').Count -or
        -not [string]::Equals([IO.Path]::GetFullPath($Test.Path), $canonical, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Reuse requires the canonical vetted fixture path.'
    }
    $onDisk = Get-Content -LiteralPath $canonical -Raw | ConvertFrom-Json
    if (($d | ConvertTo-Json -Depth 50 -Compress) -cne ($onDisk | ConvertTo-Json -Depth 50 -Compress)) {
        throw 'Reuse fixture differs from its canonical source.'
    }
}

function Open-SmokeReuseConsumer {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Sha256)
    if ($Sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $Path -notmatch '^[a-zA-Z]:[\\/]') { throw 'Reuse consumer requires absolute path and exact SHA256.' }
    if ([IO.Path]::GetFullPath($Path) -ne $Path.Replace('/', '\') -or $Path.Substring(2).Contains(':')) { throw 'Reuse consumer path alias rejected.' }
    if (-not ('PdReuseConsumerLock' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
public sealed class PdReuseConsumerLock : IDisposable {
 [StructLayout(LayoutKind.Sequential)] struct Info { public uint attrs; public System.Runtime.InteropServices.ComTypes.FILETIME c,a,w; public uint volume,high,low,links,indexHigh,indexLow; }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string p,uint access,uint share,IntPtr security,uint disposition,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info i);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFinalPathNameByHandleW(SafeFileHandle h,StringBuilder b,uint n,uint f);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr h,out long creation,out long exit,out long kernel,out long user);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint GetProcessId(IntPtr h);
 public static string QueryCreation(System.Diagnostics.Process p) { IntPtr h=p.Handle;long c,e,k,u;if(GetProcessId(h)!=(uint)p.Id||!GetProcessTimes(h,out c,out e,out k,out u))throw new IOException("Query launch handle identity unavailable");return DateTime.FromFileTimeUtc(c).ToString("o"); }
 readonly List<SafeFileHandle> parents=new List<SafeFileHandle>(); public FileStream Stream; public string Path;
 public PdReuseConsumerLock(string p) {
  try { Path=System.IO.Path.GetFullPath(p); string dir=System.IO.Path.GetDirectoryName(Path);
   while(!String.IsNullOrEmpty(dir)) { parents.Add(Open(dir,true)); string next=System.IO.Path.GetDirectoryName(dir); if(next==dir)break; dir=next; }
   Stream=new FileStream(Open(Path,false),FileAccess.Read);
  } catch { Dispose(); throw; }
 }
 SafeFileHandle Open(string p,bool directory) {
  SafeFileHandle h=CreateFileW(p,directory?0x80u:0x80000000u,directory?3u:1u,IntPtr.Zero,3,0x00200000u|(directory?0x02000000u:0u),IntPtr.Zero);
  if(h.IsInvalid){h.Dispose();throw new IOException("Consumer identity lock refused");}
  Info i; StringBuilder b=new StringBuilder(32768); uint n=GetFinalPathNameByHandleW(h,b,(uint)b.Capacity,0);
  string actual=b.ToString(); if(actual.StartsWith("\\\\?\\"))actual=actual.Substring(4);
  if(!GetFileInformationByHandle(h,out i)||(i.attrs&0x400)!=0||((i.attrs&16)!=0)!=directory||(!directory&&i.links!=1)||n==0||n>=b.Capacity||!String.Equals(actual.TrimEnd('\\'),p.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase)) {h.Dispose();throw new IOException("Consumer alias/hardlink identity rejected");}
  return h;
 }
 public string Hash() { Stream.Position=0; using(var sha=System.Security.Cryptography.SHA256.Create()){string s=BitConverter.ToString(sha.ComputeHash(Stream)).Replace("-","").ToLowerInvariant();Stream.Position=0;return s;} }
 public static System.Threading.Tasks.Task<string> ReadBounded(TextReader reader) { return System.Threading.Tasks.Task.Factory.StartNew(delegate { char[] block=new char[256]; StringBuilder result=new StringBuilder(); int count; bool overflow=false; while((count=reader.Read(block,0,block.Length))>0){if(result.Length+count<=4096)result.Append(block,0,count);else overflow=true;}if(overflow)throw new IOException("Policy query output exceeded limit");return result.ToString(); }); }
 public void Dispose(){if(Stream!=null){Stream.Dispose();Stream=null;}foreach(var h in parents)h.Dispose();parents.Clear();}
}
'@
    }
    $lock = New-Object PdReuseConsumerLock($Path)
    try { if ($lock.Hash() -ne $Sha256.ToLowerInvariant()) { throw 'Reuse consumer SHA256 mismatch.' }; return $lock }
    catch { $lock.Dispose(); throw }
}

function Get-SmokeReuseConsumerPolicy {
    param([Parameter(Mandatory)]$Consumer)
    $p = New-Object Diagnostics.Process
    $owner = $null
    try {
        $s = New-Object Diagnostics.ProcessStartInfo
        $s.FileName=$Consumer.Path; $s.Arguments='--reuse-write-policy-info'
        $s.WorkingDirectory=Split-Path -Parent $Consumer.Path
        $s.UseShellExecute=$false; $s.CreateNoWindow=$true
        $s.RedirectStandardOutput=$true; $s.RedirectStandardError=$true
        $s.StandardOutputEncoding=New-Object Text.UTF8Encoding($false)
        $s.StandardErrorEncoding=New-Object Text.UTF8Encoding($false)
        $p.StartInfo=$s
        if (-not $p.Start()) { throw 'Policy query failed to start.' }
        $script:ReuseQueryUnverified=$true
        # This query can exit before Process.Path is available. The exact Start
        # return retains its kernel handle; verify PID and creation on that
        # handle, with the executable byte/path lock held across Start.
        $created=[PdReuseConsumerLock]::QueryCreation($p)
        $owner=$null
        if (!$p.HasExited) {
            try { $owner=New-SmokeProcessOwnership -Process $p -ExpectedExecutable $Consumer.Path -CommandLineToken '--reuse-write-policy-info' }
            catch { if (!$p.HasExited) { throw } }
        }
        if (!$owner -and $p.HasExited) {
            $owner=[pscustomobject]@{schema=1;process_id=$p.Id;executable_path=$Consumer.Path;
                creation_utc=$created;parent_process_id=$PID;command_line_token='--reuse-write-policy-info'}
        }
        $script:SmokeOwnedProcesses[[int]$p.Id]=$owner
        $script:SmokeOwnedPids+= [int]$p.Id
        $stdout=[PdReuseConsumerLock]::ReadBounded($p.StandardOutput); $stderr=[PdReuseConsumerLock]::ReadBounded($p.StandardError)
        if (-not $p.WaitForExit(10000)) { [void](Stop-SmokeOwnedProcess -Ownership $owner); throw 'Policy query timed out.' }
        if ((Get-SmokeOwnedProcessState $owner).status -ne 'absent') { throw 'Policy query actor absence unverified.' }
        if (!$stdout.Wait(2000) -or !$stderr.Wait(2000)) { throw 'Policy query console drain timed out.' }
        $out=$stdout.GetAwaiter().GetResult(); $err=$stderr.GetAwaiter().GetResult()
        if ($p.ExitCode -ne 0 -or $err.Trim() -or $out.Trim() -notmatch '^\{"schema":"pd2\.native-write-policy\.v1","supported":true\}$') { throw 'Linked native write-policy query refused.' }
        return @{schema='pd2.native-write-policy.v1';supported=$true;exit_code=0;stdout=$out.Trim()}
    } finally {
        if ($owner -and (Get-SmokeOwnedProcessState $owner).status -ne 'absent') { [void](Stop-SmokeOwnedProcess -Ownership $owner) }
        if ($owner -and (Get-SmokeOwnedProcessState $owner).status -eq 'absent') {
            $script:SmokeOwnedPids=@($script:SmokeOwnedPids | Where-Object { $_ -ne [int]$owner.process_id })
            $script:ReuseQueryUnverified=$false
        }
        $p.Dispose()
    }
}
