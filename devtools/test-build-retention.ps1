#Requires -Version 5.1
param([Parameter(Mandatory)][string]$EvidenceDirectory,[Parameter(Mandatory)][string]$BeforeWrapper)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Build-Retention.ps1')
$canonical=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$evidence=[IO.Path]::GetFullPath($EvidenceDirectory)
Assert-SmokePlainTree -Path $evidence
if(-not $evidence.StartsWith($canonical+'\.claude\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path $evidence)){throw 'Fresh canonical .claude fixture required.'}
[void][IO.Directory]::CreateDirectory($evidence)
$checks=New-Object 'Collections.Generic.List[string]'
function Check([bool]$Ok,[string]$Label){if(-not $Ok){throw "Failed: $Label"};$checks.Add($Label)}
function Refuses([scriptblock]$Action,[string]$Pattern,[string]$Label){
    $refused=$false
    try{& $Action|Out-Null}catch{if($_.Exception.Message -notmatch $Pattern){throw};$refused=$true}
    Check $refused $Label
}
$wrapper=Join-Path $PSScriptRoot 'build-session.ps1'
# Inspect structure before executing any wrapper probe. Never invoke the
# canonical full wrapper: each probe uses an independently guarded fixture.
$tokens=$null;$errors=$null
$wrapperText=[IO.File]::ReadAllText($wrapper)
# Parse exactly the decoded string whose offsets are edited below. Windows
# PowerShell ParseFile can decode a BOM-less UTF8 file as ANSI and shift offsets.
$ast=[Management.Automation.Language.Parser]::ParseInput($wrapperText,[ref]$tokens,[ref]$errors)
Check (@($errors).Count -eq 0) 'wrapper parse'
$old=[Management.Automation.Language.Parser]::ParseInput([IO.File]::ReadAllText([IO.Path]::GetFullPath($BeforeWrapper)),[ref]$tokens,[ref]$errors)
Check (@($errors).Count -eq 0) 'baseline parse'
$self=[Management.Automation.Language.Parser]::ParseFile($PSCommandPath,[ref]$tokens,[ref]$errors)
Check (@($errors).Count -eq 0) 'test parse before wrapper execution'
$entryCalls=@($self.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and
    $n.CommandElements[0].Extent.Text -ceq '$wrapper'},$true))
Check ($entryCalls.Count -eq 0) 'no canonical full-wrapper invocation in test'
function Fn($Tree,[string]$Name){$Tree.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $Name},$true)}
foreach($name in @('Test-PidAlive','Get-ChildProcessIds','Stop-ProcessTree','Show-ProcessTreeSnapshot',
    'Get-BuildProgressSignal','Test-BuildProgressAdvanced','Get-QueueTimeoutSeconds','Get-SafeChildPath','ConvertTo-SafeSessionName')){
    Check ((Fn $ast $name).Extent.Text.Replace("`r`n","`n") -ceq (Fn $old $name).Extent.Text.Replace("`r`n","`n")) ("excluded algorithm unchanged "+$name)
}
foreach($command in @('Remove-Item','Stop-Process')){
    $oldNodes=@($old.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq $command},$true)|ForEach-Object {$_.Extent.Text.Replace("`r`n","`n")})
    $newNodes=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq $command},$true)|ForEach-Object {$_.Extent.Text.Replace("`r`n","`n")})
    Check (($oldNodes -join "`n") -ceq ($newNodes -join "`n")) ("no new or altered "+$command+" callsite")
}
$oldStart=$old.Find({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Start-Process'},$true)
$newStart=$ast.Find({param($n)$n -is [Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Start-Process'},$true)
Check ($oldStart.Extent.Text.Replace("`r`n","`n") -ceq $newStart.Extent.Text.Replace("`r`n","`n")) 'native child launch command unchanged'

$launches=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and
    $n.GetCommandName() -in @('Start-Process','powershell.exe')},$true))
Check ($launches.Count -eq 2) 'exact two native launch sites identified before fixture execution'
$dynamicCalls=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and
    $n.InvocationOperator -eq [Management.Automation.Language.TokenKind]::Ampersand},$true))
Check ($dynamicCalls.Count -eq 1 -and $dynamicCalls[0].GetCommandName() -ceq 'powershell.exe') 'no additional dynamic launch route admitted'
Check (@($ast.FindAll({param($n)$n -is [Management.Automation.Language.InvokeMemberExpressionAst] -and
    $n.Member.Extent.Text -match '^(Start|Kill)$'},$true)).Count -eq 0) 'no direct process Start or Kill method admitted'
$maintenance=@($ast.EndBlock.Statements|Where-Object {$_.Extent.Text.StartsWith('if ($SelfTest)')})
$setup=@($ast.EndBlock.Statements|Where-Object {$_.Extent.Text.StartsWith('if($RetainHousekeeping){') -and
    $_.Extent.Text.Contains('Set-SmokeStorageRetention -RetainHousekeeping')})
Check ($maintenance.Count -eq 1 -and $setup.Count -eq 1) 'independent barriers located before maintenance and setup'
$barrierDefinition=@'
$global:BuildTestBarrierHits=0
function global:Invoke-BuildTestLaunchBarrier {
    $global:BuildTestBarrierHits++
    throw 'Independent test boundary: no setup, maintenance or native launch admitted.'
}
'@
$edits=@(@{Start=$ast.EndBlock.Statements[0].Extent.StartOffset;Length=0;Text=($barrierDefinition+"`n")},
    @{Start=$maintenance[0].Extent.StartOffset;Length=0;Text='if($SelfTest -or $Remove -or $RemoveAll){Invoke-BuildTestLaunchBarrier}'+"`n"},
    @{Start=$setup[0].Extent.StartOffset;Length=0;Text="Invoke-BuildTestLaunchBarrier`n"})
foreach($launch in $launches){$edits+=@{Start=$launch.Extent.StartOffset;Length=$launch.Extent.EndOffset-$launch.Extent.StartOffset;Text='Invoke-BuildTestLaunchBarrier'}}
$instrumented=$wrapperText
$orderedEdits=@($edits|Sort-Object -Property {[int]$_['Start']} -Descending)
$previous=[int]::MaxValue;$descending=$true
foreach($edit in $orderedEdits){if([int]$edit['Start'] -ge $previous){$descending=$false};$previous=[int]$edit['Start']}
Check $descending 'fixture edits sorted by explicit numeric dictionary key before instrumentation'
foreach($edit in $orderedEdits){$instrumented=$instrumented.Remove([int]$edit['Start'],[int]$edit['Length']).Insert([int]$edit['Start'],[string]$edit['Text'])}
$safeAst=[Management.Automation.Language.Parser]::ParseInput($instrumented,[ref]$tokens,[ref]$errors)
Check (@($errors).Count -eq 0) 'instrumented full-wrapper fixture parses before execution'
Check (@($safeAst.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst] -and
    ($n.GetCommandName() -in @('Start-Process','powershell.exe') -or
     $n.InvocationOperator -eq [Management.Automation.Language.TokenKind]::Ampersand)},$true)).Count -eq 0) 'fixture has no original process launch route'
$probeResults=New-Object 'Collections.Generic.List[object]'
function Invoke-IsolatedBuildWrapper([hashtable]$Options,[switch]$DisableRefusals) {
    $root=Join-Path $evidence ('entry-probe-'+[guid]::NewGuid().ToString('N'))
    $scriptDir=Join-Path $root 'devtools'
    $lib=Join-Path $root 'tools/smoke-verify/lib'
    [void][IO.Directory]::CreateDirectory($scriptDir)
    [void][IO.Directory]::CreateDirectory($lib)
    $copy=Join-Path $scriptDir 'build-session.ps1'
    [IO.File]::WriteAllText($copy,$instrumented,(New-Object Text.UTF8Encoding($true)))
    # No real builder or backend is copied into this fixture project.
    [IO.File]::WriteAllText((Join-Path $scriptDir 'build-headless.ps1'),"throw 'Inert test builder: native work forbidden.'")
    $helper=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Build-Retention.ps1'))
    if($DisableRefusals){
        $helperAst=[Management.Automation.Language.Parser]::ParseInput($helper,[ref]$tokens,[ref]$errors)
        if(@($errors).Count){throw 'Helper fault fixture parse failed.'}
        $faults=@(foreach($name in @('Assert-BuildRetentionOptions','Assert-BuildRetainedChild')){
            $fn=Fn $helperAst $name
            if($null -eq $fn){throw 'Missing production refusal for fault injection.'}
            $fn.Body.Extent
        })
        foreach($fault in $faults|Sort-Object StartOffset -Descending){
            $helper=$helper.Remove($fault.StartOffset,$fault.EndOffset-$fault.StartOffset).Insert($fault.StartOffset,'{}')
        }
    }
    [IO.File]::WriteAllText((Join-Path $scriptDir 'Build-Retention.ps1'),$helper)
    # New shared frontend dependency; only definitions are copied. The existing
    # independent full-wrapper barriers remain before setup and native factories.
    [IO.File]::WriteAllText((Join-Path $lib 'Runner-Supervisor.ps1'),[IO.File]::ReadAllText((Join-Path $PSScriptRoot '../tools/smoke-verify/lib/Runner-Supervisor.ps1')))
    foreach($name in @('Storage-Harness.ps1','Runner-Retention.ps1')){
        [IO.File]::WriteAllBytes((Join-Path $lib $name),[IO.File]::ReadAllBytes((Join-Path $canonical ('tools/smoke-verify/lib/'+$name))))
    }
    [IO.File]::WriteAllBytes((Join-Path $root 'tools/smoke-verify/storage-policy.json'),
        [IO.File]::ReadAllBytes((Join-Path $canonical 'tools/smoke-verify/storage-policy.json')))
    $runspace=[Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
    $shell=[Management.Automation.PowerShell]::Create()
    $failure=$null
    try{
        $runspace.Open();$shell.Runspace=$runspace
        [void]$shell.AddCommand($copy).AddParameters($Options)
        try{[void]$shell.Invoke()}catch{$failure=$_.Exception}
        $messages=@($shell.Streams.Error|ForEach-Object {$_.Exception.Message})
        if($shell.HadErrors -and $shell.Streams.Error.Count){$failure=$shell.Streams.Error[0].Exception}
        if($null -ne $failure -and $messages.Count -eq 0){$messages=@($failure.Message)}
        $shell.Commands.Clear();$shell.Streams.Error.Clear()
        [void]$shell.AddScript('$global:BuildTestBarrierHits')
        $hitValues=@($shell.Invoke())
        if($shell.HadErrors -or $hitValues.Count -ne 1){throw 'Independent barrier counter query unverified.'}
        $hits=[int]$hitValues[0]
        $receipt=@{root=$root;options=$Options;production_refusals_disabled=[bool]$DisableRefusals;
            replaced_launch_sites=$launches.Count;independent_boundary_hits=$hits;errors=$messages;
            inert_builder=$true;real_backend_absent=$true;canonical_wrapper_invoked=$false}
        $probeResults.Add($receipt)
        [IO.File]::WriteAllText((Join-Path $root 'probe-result.json'),(ConvertTo-Json -InputObject $receipt -Depth 10))
    }finally{$shell.Dispose();$runspace.Dispose()}
    if($null -ne $failure){throw $failure}
    throw 'Isolated wrapper unexpectedly completed without refusal.'
}
foreach($mode in @('Clean','NoQueue','Remove','RemoveAll','SelfTest','Force','List')){
    $entryOptions=@{RetainHousekeeping=$true;Session='inert';$mode=$true}
    Refuses {Invoke-IsolatedBuildWrapper -Options $entryOptions} ("refuses "+$mode) ("isolated entrypoint early refusal "+$mode)
}
Refuses {Invoke-IsolatedBuildWrapper -Options @{RetainHousekeeping=$true;Session='inert';DevMods='all'}} 'refuses DevMods' 'isolated entrypoint devmods refusal'
Refuses {Invoke-IsolatedBuildWrapper -Options @{RetainHousekeeping=$true}} 'explicit stable Session' 'isolated entrypoint explicit session required'
Refuses {Invoke-IsolatedBuildWrapper -Options @{RetainHousekeeping=$true;Session='inert'}} 'no released, reviewed exact capability' 'isolated real child capability refusal before independent boundary'
Check ($probeResults.Count -eq 10) 'all normal entrypoint probes have actual receipts'
Check (@($probeResults|Where-Object {$_.independent_boundary_hits -ne 0}).Count -eq 0) 'production refusals precede independent barriers in all normal probes'
Refuses {Invoke-IsolatedBuildWrapper -Options @{Session='inert'}} 'Independent test boundary' 'missing retained switch still cannot reach setup or native launch'
Refuses {Invoke-IsolatedBuildWrapper -Options @{RetainHousekeeping=$true;Session='inert'} -DisableRefusals} 'Independent test boundary' 'disabled production refusal functions still cannot launch native work'
Refuses {Invoke-IsolatedBuildWrapper -Options @{RetainHousekeeping=$true;Session='inert';SelfTest=$true} -DisableRefusals} 'Independent test boundary' 'disabled maintenance refusal cannot enter temporary cleanup'
Check ($probeResults.Count -eq 13) 'all thirteen isolated entrypoint probes have actual receipts'
Check (@($probeResults|Select-Object -Last 3|Where-Object {$_.independent_boundary_hits -eq 1}).Count -eq 3) 'all gate-regression probes hit the independent barrier exactly once'
Check (@($probeResults|Where-Object {Test-Path (Join-Path $_.root '.claude')}).Count -eq 0) 'all full-wrapper probes create no build queue, lock, context or output root'
Assert-BuildRetentionOptions -Retained $false -Modes @{Clean=$true;NoQueue=$true} -DevMods all -Session ''
$checks.Add('default option behavior accepted')
# Load actual production functions. All state is rebound to this fixture.
$definitions=@($ast.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.FunctionDefinitionAst]}|ForEach-Object {$_.Extent.Text})
. ([scriptblock]::Create(($definitions -join "`n")))
$script:SmokeStoragePolicy=@{min_free_gib=0;max_full_installs=5;max_full_install_gib=.01;max_archive_gib=.01;peak_run_gib=0;extra_growth_gib=0;peak_build_gib=0}
$Target='tests';$BuildTimeoutSeconds=120;$QueueTimeoutExitCode=124
function NewFixture([string]$Name){
    $script:ProjectDir=Join-Path $evidence $Name
    [void][IO.Directory]::CreateDirectory($ProjectDir)
    $script:SessionBuildRoot=Join-Path $ProjectDir '.claude/session-builds'
    $script:QueueDir=Join-Path $SessionBuildRoot '.queue'
    $script:LockDir=Join-Path $SessionBuildRoot '.locks'
    $script:QueueLockPath=Join-Path $QueueDir 'queue.lock'
    $script:QueueActivePath=Join-Path $QueueDir 'active.json'
    $script:QueueDurationsPath=Join-Path $QueueDir 'durations.json'
    $script:RetainHousekeeping=$true
    $script:BuildRetentionContext=New-BuildRetentionInvocation -BuildDir (Join-Path $SessionBuildRoot 'owned') -SessionName owned
    $script:BuildRetentionChild=$null
    $script:BuildRetentionCapture=$null
    $script:BuildRetentionCloseout=$null
    $script:BuildRetentionReceiptWorker=$null
    $script:BuildRetentionFailure=$null
}
function Bytes([string]$Path){[Convert]::ToBase64String([IO.File]::ReadAllBytes($Path))}
function Receipts{
    @(Get-ChildItem -LiteralPath $script:BuildRetentionContext.RunRoot -File -Filter 'protocol-*.json'|ForEach-Object {Get-Content $_.FullName -Raw|ConvertFrom-Json})
}
NewFixture success
$token=Enter-BuildQueue owned tests (Join-Path $SessionBuildRoot 'owned') 5 120
Check ((Read-QueueJson $QueueActivePath).RequestId -ceq $token.RequestId) 'actual FIFO claim remains active'
$requestReceipt=@(Receipts|Where-Object phase -eq 'before-own-release')
Check ($requestReceipt.Count -eq 1 -and $requestReceipt[0].raw_base64.Length -gt 0) 'exact request archived before existing own protocol release'
$lock=Enter-SessionBuildLock owned
$lockBytes=New-Object byte[] ([int]$lock.Stream.Length)
$lock.Stream.Position=0;[void]$lock.Stream.Read($lockBytes,0,$lockBytes.Length)
$lockBase64=[Convert]::ToBase64String($lockBytes)
Assert-BuildRetainedCloseout $token
Exit-SessionBuildLock $lock
Check (-not (Test-Path $lock.Path)) 'existing owned lock release remains functional'
Check (@(Receipts|Where-Object {$_.phase -eq 'before-own-lock-release' -and $_.raw_base64 -ceq $lockBase64}).Count -eq 1) 'exact owned lock bytes retained'
$activeBytes=Bytes $QueueActivePath
Exit-BuildQueue $token 7
Check (-not (Test-Path $QueueActivePath)) 'existing own queue release remains functional'
Check (@(Receipts|Where-Object {$_.phase -eq 'before-own-release' -and $_.raw_base64 -ceq $activeBytes}).Count -eq 1) 'exact active bytes retained before release'
Check ((Read-QueueJson $QueueDurationsPath).ExitCode -eq 7) 'failed verdict preserved in duration history'
Check ((Get-BuildStdoutLogPath 'unused') -ceq (Join-Path $script:BuildRetentionContext.RunRoot '_build-session.out.log')) 'stdout uses unique accounted run root'
[IO.File]::WriteAllText((Get-BuildStdoutLogPath 'unused'),'actual retained log')
$signal=Get-BuildProgressSignal (Join-Path $SessionBuildRoot 'owned')
Check ($signal.Bytes -gt 0) 'unchanged watchdog reads actual retained log'
$tail=@(Show-BuildRetainedTail -SessionName owned -Lines 10 -Follow $false)
Check (($tail -join '') -match 'actual retained log') 'retained tail resolves same invocation log'
$status=Invoke-SmokeStorage -Action status -ProjectRoot $ProjectDir
Check ($status.ArchiveBytes -gt 0) 'protocol receipts charged by existing archive domain'
Check (-not (Test-Path (Join-Path $ProjectDir '.claude/smoke-storage/state.json'))) 'protocol does not create a second registry'

NewFixture collision
[void][IO.Directory]::CreateDirectory($QueueDir)
$stateLock=Enter-QueueStateLock
try{
    $path=Join-Path $QueueDir 'fixed.request.json'
    $record=[pscustomobject]@{RequestId='fixed';WrapperPid=$PID;RetentionRunId=$script:BuildRetentionContext.RunId}
    Write-JsonFile $path $record
    $prior=Bytes $path
    Refuses {Write-JsonFile $path $record} 'output collision' 'native existing request collision'
    Check ((Bytes $path) -ceq $prior) 'collision preserves prior bytes'
    $foreign=[pscustomobject]@{RequestId='foreign';WrapperPid=0;RetentionRunId=$script:BuildRetentionContext.RunId}
    Refuses {Write-JsonFile (Join-Path $QueueDir 'foreign.request.json') $foreign} 'not this invocation owned record' 'foreign new owner refused'
    Refuses {Write-JsonFile (Join-Path $SessionBuildRoot 'foreign.request.json') $record} 'Unknown retained queue output' 'internal path escape refused'
}finally{Exit-QueueStateLock $stateLock}
Refuses {Write-JsonFile (Join-Path $QueueDir 'no-lock.request.json') $record} 'canonical queue state lock' 'queue mutex required before writes'
[void][IO.Directory]::CreateDirectory($LockDir)
$existing=Join-Path $LockDir 'peer.lock'
[IO.File]::WriteAllText($existing,'peer owned lock')
Refuses {Enter-SessionBuildLock peer} 'session lock collision' 'peer lock collision before binding'
Check (-not (Test-BuildProtocolRetained $existing)) 'peer lock acquired no ownership binding'

NewFixture malformed
$stateLock=Enter-QueueStateLock
try{
    Write-JsonFile $QueueActivePath ([pscustomobject]@{RequestId='owned';WrapperPid=$PID;ChildPid=0;RetentionRunId=$script:BuildRetentionContext.RunId})
}finally{Exit-QueueStateLock $stateLock}
[IO.File]::WriteAllText($QueueActivePath,'{ malformed')
$corrupt=Bytes $QueueActivePath
$script:RetainHousekeeping=$false;$script:BuildRetentionContext=$null
Refuses {Clear-StaleQueueState} 'malformed active queue state' 'default caller preserves corrupt retained active binding'
Check ((Bytes $QueueActivePath) -ceq $corrupt) 'corrupt retained bytes untouched'

NewFixture stale
[void][IO.Directory]::CreateDirectory($QueueDir)
$path=Join-Path $QueueDir 'stale.request.json'
[IO.File]::WriteAllText($path,'{"WrapperPid":0}')
$prior=Bytes $path
Refuses {Clear-StaleQueueState} 'stale or malformed queue request' 'retained stale request refused before pruning'
Check ((Bytes $path) -ceq $prior) 'stale request preserved'

NewFixture orphan
$stateLock=Enter-QueueStateLock
try {Write-JsonFile $QueueActivePath ([pscustomobject]@{RequestId='orphan';WrapperPid=$PID;ChildPid=42;RetentionRunId=$script:BuildRetentionContext.RunId})}
finally {Exit-QueueStateLock $stateLock}
$prior=Bytes $QueueActivePath
$realAlive=(Get-Command Test-PidAlive).ScriptBlock
function Test-PidAlive {param($pidValue) return $pidValue -eq 42}
$script:RetainHousekeeping=$false
try {Refuses {Clear-StaleQueueState} 'orphaned queue actor' 'default caller refuses retained orphan without stop'}
finally {Set-Item Function:Test-PidAlive $realAlive}
Check ((Bytes $QueueActivePath) -ceq $prior) 'orphan active bytes preserved'

NewFixture history
$token=Enter-BuildQueue owned tests (Join-Path $SessionBuildRoot 'owned') 5 120
$stateLock=Enter-QueueStateLock
try{
    $rows=@(1..41|ForEach-Object {[pscustomobject]@{Session=('old-'+$_);Target='tests';ExitCode=1;DurationSeconds=1;CompletedUtc=[DateTime]::UtcNow.ToString('o')}})
    Write-JsonFile $QueueDurationsPath $rows
    Add-BuildDurationRecord $token 0
    Check (@(Read-BuildRetainedDurationHistory).Count -eq 42) 'retained duration history is not truncated at forty'
    foreach($bad in @('{ broken','null','[{"Target":"tests"}]')){
        [IO.File]::WriteAllText($QueueDurationsPath,$bad)
        $prior=Bytes $QueueDurationsPath
        Refuses {Add-BuildDurationRecord $token 0} 'malformed or unsupported duration history' 'malformed history refused before append'
        Check ((Bytes $QueueDurationsPath) -ceq $prior) 'malformed history bytes preserved'
    }
}finally{Exit-QueueStateLock $stateLock}

NewFixture bindingAlias
# Use an existing junction read-only; do not create one or alter permissions.
# Inject only the computed directory, then exercise the real binding writer.
$aliasAncestor=Get-Item -LiteralPath 'C:\Users\All Users' -Force
Check (($aliasAncestor.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) 'read-only alias ancestor is an actual reparse point'
$script:AliasBindingDirectory=Join-Path $aliasAncestor.FullName ('pd-retention-fixture-'+[guid]::NewGuid().ToString('N'))
Check (-not (Test-Path -LiteralPath $script:AliasBindingDirectory)) 'alias destination absent before refusal'
$realBindingDirectory=(Get-Command Get-BuildProtocolBindingDirectory).ScriptBlock
function Get-BuildProtocolBindingDirectory {param([string]$Path) return $script:AliasBindingDirectory}
try{
    Refuses {New-BuildProtocolBinding (Join-Path $QueueDir 'alias.request.json')} 'alias|reparse|link' 'actual binding writer refuses a computed destination with a junction ancestor'
    Check (-not (Test-Path -LiteralPath $script:AliasBindingDirectory)) 'alias refusal creates no redirected directory or marker'
}finally{Set-Item Function:Get-BuildProtocolBindingDirectory $realBindingDirectory}

NewFixture snapshotFailure
$token=Enter-BuildQueue owned tests (Join-Path $SessionBuildRoot 'owned') 5 120
$activeBytes=Bytes $QueueActivePath
$realReceipt=(Get-Command Write-BuildProtocolReceipt).ScriptBlock
function Write-BuildProtocolReceipt {throw 'synthetic snapshot publication failure'}
try{
    Refuses {Exit-BuildQueue $token 0} 'synthetic snapshot publication failure' 'snapshot failure blocks own queue release'
    Check ((Bytes $QueueActivePath) -ceq $activeBytes) 'active identity preserved after snapshot failure'
}finally{Set-Item Function:Write-BuildProtocolReceipt $realReceipt}
# This fixture's retained active record intentionally remains for inspection.

NewFixture unverified
$token=Enter-BuildQueue owned tests (Join-Path $SessionBuildRoot 'owned') 5 120
$stateLock=Enter-QueueStateLock
try{
    $active=Read-QueueJson $QueueActivePath;$active.ChildPid=99;Write-JsonFile $QueueActivePath $active
}finally{Exit-QueueStateLock $stateLock}
$prior=Bytes $QueueActivePath
Refuses {Assert-BuildRetainedCloseout $token} 'child closeout unverified' 'unknown child cannot become inferred absence'
Check ((Bytes $QueueActivePath) -ceq $prior) 'unverified closeout keeps queue evidence'

# New N0589 parent receipt and finite-closeout cases. These mocks never launch
# the capture factory, a native child, Blender, game or C/C++ build. Managed
# source compilation is required on each normally admitted supported host.
NewFixture parentCloseout
Initialize-BuildParentCaptureType
Add-Type -TypeDefinition @'
using System;
using System.Threading.Tasks;
public static class PdParentCloseoutTestTasks {
    public static Task<object> Ready(object value) { return Task.FromResult(value); }
    public static Task<object> Stalled() { return new TaskCompletionSource<object>().Task; }
    public static Task<object> Failed(string message) {
        var task=new TaskCompletionSource<object>();task.SetException(new Exception(message));return task.Task;
    }
    public static Task<object> Delayed(object value,int milliseconds) {
        return Task.Run(async()=>{await Task.Delay(milliseconds);return value;});
    }
}
'@
$checks.Add('actual capture managed CSharp source compiled on '+$PSVersionTable.PSVersion.ToString()+' without capture construction')
# Exercise the actual checked-close primitive with inert delegates, never an OS
# handle. Failed assignment preserves its witness; partial close/retry is explicit.
$fakeCloseOk=[Func[IntPtr,bool]]{param($handle) return $true}
$fakeCloseFail=[Func[IntPtr,bool]]{param($handle) return $false}
$fakeCloseError=[Func[int]]{return 5}
$witness=[IntPtr]123
Refuses {$witness=[PdBuildParentCapture]::CheckedClose($witness,$fakeCloseFail,$fakeCloseError)} 'Close exact owned handle' 'actual managed close primitive refuses failure'
Check ($witness -eq [IntPtr]123) 'failed close preserves exact witness'
$first=[PdBuildParentCapture]::CheckedClose([IntPtr]122,$fakeCloseOk,$fakeCloseError)
$second=[IntPtr]124
try{$second=[PdBuildParentCapture]::CheckedClose($second,$fakeCloseFail,$fakeCloseError)}catch{
    Check ($_.Exception.GetBaseException().NativeErrorCode -eq 5) 'close failure preserves native error code'
}
Check ($first -eq [IntPtr]::Zero -and $second -eq [IntPtr]124) 'partial close keeps remaining owned witness'
$second=[PdBuildParentCapture]::CheckedClose($second,$fakeCloseOk,$fakeCloseError)
Check ($second -eq [IntPtr]::Zero) 'explicit owned retry clears only successful witness'
[PdBuildParentCapture]::CheckReceiptBytes(8,8,16)
$checks.Add('actual receipt memory primitive accepts exact combined bound')
foreach($sizes in @(@(9,8,16),@([long]::MaxValue,8,16),@(-1,8,16),@(8,-1,16),@(8,8,0))){
    Refuses {[PdBuildParentCapture]::CheckReceiptBytes($sizes[0],$sizes[1],$sizes[2])} 'memory ceiling' 'actual receipt memory primitive rejects oversize/overflow/invalid bound'
}
$proofFields=@{Schema='pd2.build-parent-closeout.v1';RequestId='req';RunId=$script:BuildRetentionContext.RunId;
    DomainId='domain';RootPid=99;CreationUtc='2026-10-12T12:00:00Z';WrapperPid=$PID;
    CaptureSourceSha256=('a'*64);RootExited=$true;DomainActive=0;
    StdoutEof=$true;StderrEof=$true;Flushed=$true;ExitCode=17}
$proof=[pscustomobject]$proofFields
Assert-BuildParentCloseoutFields $proof 'req' $script:BuildRetentionContext.RunId 99 $proof.CreationUtc 'domain'
$checks.Add('complete parent field contract accepted without native actor')
foreach($case in @(@('RootExited',$false),@('DomainActive',1),@('DomainActive',$false),
    @('StdoutEof',$false),@('StderrEof',$false),@('Flushed',$false),
    @('RequestId','foreign'),@('RunId','foreign'),@('DomainId','foreign'),
    @('RootPid',100),@('CreationUtc','foreign'),@('CaptureSourceSha256',''),@('WrapperPid',($PID+1)))){
    $bad=[pscustomobject]$proofFields.Clone();$bad.($case[0])=$case[1]
    Refuses {Assert-BuildParentCloseoutFields $bad 'req' $proof.RunId 99 $proof.CreationUtc 'domain'} 'domain/EOF closeout unverified' ('parent rejects '+$case[0]+':'+$case[1])
}
function FakeParentCapture {
    # Independent inert fixture owners; no real capture/runspace is abandoned.
    # Same-capture recovery below deliberately does not call this reset helper.
    $script:BuildRetentionCloseoutOperation=$null;$script:BuildRetentionReceiptWorker=$null
    $script:BuildRetentionFailure=$null
    $fakeProof=[pscustomobject]$proofFields.Clone();$fakeProof.DomainId=[guid]::NewGuid().ToString('N')
    $fake=[pscustomobject]@{Id=99;CreationUtc=$proof.CreationUtc;DomainId=$fakeProof.DomainId;RequestId='req';
        RunId=$proof.RunId;StdoutPath='mock-out';StderrPath='mock-err';PumpFailed=$false;
        StdoutEof=$true;StderrEof=$true;RootDone=$true;DomainCount=0;CloseoutProof=$fakeProof;Disposals=0;FailDispose=$false;
        SnapshotStalled=$false;SnapshotFails=$false;SnapshotDelay=0;SnapshotTask=$null;DisposeStalled=$false;
        Launched=$true;LaunchFailure=$null;CleanupFailure=$null}
    $fake|Add-Member ScriptMethod WaitForExit {param($ms) return $this.RootDone}
    $fake|Add-Member ScriptMethod ActiveProcesses {return $this.DomainCount}
    $fake|Add-Member ScriptMethod BeginSnapshot {param($maximum)
        $data=[pscustomobject]@{Proof=$this.CloseoutProof;StdoutBase64='AQID';StderrBase64='AQID'}
        if($this.SnapshotStalled){$this.SnapshotTask=[PdParentCloseoutTestTasks]::Stalled()}
        elseif($this.SnapshotFails){$this.SnapshotTask=[PdParentCloseoutTestTasks]::Failed('Parent logs exceed bounded receipt memory ceiling')}
        elseif($this.SnapshotDelay){$this.SnapshotTask=[PdParentCloseoutTestTasks]::Delayed($data,$this.SnapshotDelay)}
        else{$this.SnapshotTask=[PdParentCloseoutTestTasks]::Ready($data)}
        return $this.SnapshotTask
    }
    $fake|Add-Member ScriptMethod BeginDisposeClosed {
        if($this.FailDispose){$this.CleanupFailure=[pscustomobject]@{Stage='close root witness';NativeError=5;Message='synthetic stream disposal failure'};return [PdParentCloseoutTestTasks]::Failed('synthetic stream disposal failure')}
        if($this.DisposeStalled){return [PdParentCloseoutTestTasks]::Stalled()}
        $this.Disposals++;return [PdParentCloseoutTestTasks]::Ready($null)
    }
    return $fake
}
$realStartParentPublisher=(Get-Command Start-BuildParentReceiptWorker).ScriptBlock
$realReadParentPublisher=(Get-Command Read-BuildParentReceiptWorker).ScriptBlock
$script:ParentPublisherFails=$false;$script:ParentPublisherStalled=$false;$script:ParentPublisherDelay=0
$script:ParentCompletionVerifyDelay=0
$script:ParentCompletionPublishDelay=0
$script:ParentVerificationNames=New-Object 'Collections.Generic.List[string]'
function Start-BuildParentReceiptWorker($Capture,$Data,[string]$Name='', $Value=$null,$ExistingReceipt=$null){
    if(-not $Name){$Name='parent-closeout-'+$Capture.DomainId+'.json'}
    $worker=[pscustomobject]@{Complete=$false;Task=$null;Data=$Data;Capture=$Capture;Result=$null;Failure=$script:ParentPublisherFails;
        Name=$Name;Value=$Value;ExistingReceipt=$ExistingReceipt}
    if($script:ParentPublisherStalled){$worker.Task=[PdParentCloseoutTestTasks]::Stalled()}
    else{
        $delay=if($Name.EndsWith('-completion.json')){if($null -ne $ExistingReceipt){$script:ParentCompletionVerifyDelay}else{$script:ParentCompletionPublishDelay}}else{$script:ParentPublisherDelay}
        $worker.Task=[PdParentCloseoutTestTasks]::Delayed('ready',$delay)
    }
    $script:BuildRetentionReceiptWorker=$worker
    return $worker
}
function Read-BuildParentReceiptWorker($Work){
    if($Work.Failure){throw 'synthetic parent accounting failure'}
    if(-not $Work.Task.IsCompleted){return}
    if(-not $Work.Complete){
        if($null -ne $Work.ExistingReceipt){
            if((Get-FileHash -LiteralPath $Work.ExistingReceipt.Path).Hash.ToLowerInvariant() -cne $Work.ExistingReceipt.ReceiptSha256){throw 'Parent immutable receipt changed.'}
            $script:ParentVerificationNames.Add($Work.Name);$Work.Result=$Work.ExistingReceipt
        }else{
            $path=Join-Path $evidence ('fake-accounted-'+$Work.Name)
            $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
            try{$bytes=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json $Work.Value -Depth 50));$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
            $Work.Result=[pscustomobject]@{Path=$path;ReceiptSha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}
        }
        $Work.Complete=$true
    }
    return $Work.Result
}
try{
$fake=FakeParentCapture
$result=Complete-BuildParentCapture $fake 1000
Check ($result.ExitCode -eq 17 -and $fake.Disposals -eq 1) 'parent retains authoritative nonzero root code'
Check ([object]::ReferenceEquals($script:BuildRetentionCloseout.Proof,$fake.CloseoutProof) -and (Test-Path $script:BuildRetentionCloseout.Path)) 'parent accepts fake publisher receipt before release record, not native accounting proof'
foreach($case in @(@('RootDone',$false),@('DomainCount',1),@('StdoutEof',$false),@('StderrEof',$false),@('PumpFailed',$true),
    @('FailDispose',$true),@('SnapshotStalled',$true),@('SnapshotFails',$true),@('DisposeStalled',$true))){
    $fake=FakeParentCapture;$fake.($case[0])=$case[1];$script:BuildRetentionCloseout=$null
    Refuses {Complete-BuildParentCapture $fake 50} 'deadline|pump failed|synthetic stream disposal|memory ceiling' ('finite parent failure '+$case[0])
    Check ($null -eq $script:BuildRetentionCloseout -and $fake.Disposals -eq 0) ('parent retains unknown capture '+$case[0])
}
$script:ParentPublisherFails=$true
    $fake=FakeParentCapture;$script:BuildRetentionCloseout=$null
    Refuses {Complete-BuildParentCapture $fake 1000} 'synthetic parent accounting failure' 'parent accounting failure cannot release capture'
    Check ($null -eq $script:BuildRetentionCloseout -and $fake.Disposals -eq 0) 'accounting failure preserves held capture'
$script:ParentPublisherFails=$false;$script:ParentPublisherStalled=$true
$fake=FakeParentCapture;$script:BuildRetentionCloseout=$null
$watch=[Diagnostics.Stopwatch]::StartNew()
Refuses {Complete-BuildParentCapture $fake 50} 'deadline' 'stalled publisher and failure reporting return Unknown'
Check ($watch.ElapsedMilliseconds -lt 1000 -and -not $script:BuildRetentionReceiptWorker.Complete -and $fake.Disposals -eq 0) 'timeout retains explicit publisher ownership without retry/disposal'
$script:ParentPublisherStalled=$false;$script:ParentPublisherDelay=5
$fake=FakeParentCapture;$fake.SnapshotDelay=5;$script:BuildRetentionCloseout=$null
Check ((Complete-BuildParentCapture $fake 1000).ExitCode -eq 17) 'delayed flush and delayed publication can complete within one budget'
$script:ParentPublisherDelay=0
$fake=FakeParentCapture;$fake.SnapshotDelay=150;$script:BuildRetentionCloseout=$null
Refuses {Complete-BuildParentCapture $fake 50} 'deadline' 'late flush completion cannot retrospectively close protocol'
Check ($fake.SnapshotTask.Wait(1000) -and $null -eq $script:BuildRetentionCloseout -and $fake.Disposals -eq 0) 'late managed snapshot ends but ownership still requires explicit closeout'
$fake=FakeParentCapture;$script:BuildRetentionCloseout=$null
$originalLaunch=[pscustomobject]@{stage='identity';native_error=5;message='injected original launch cause'}
$fake.RootDone=$false
Refuses {Complete-BuildParentCapture $fake 50 -LaunchError $originalLaunch} 'deadline' 'incomplete closeout retains original structured launch error'
Check ([object]::ReferenceEquals($script:BuildRetentionFailure.launch_error,$originalLaunch)) 'original launch evidence survives closeout timeout'

# A new allowed window resumes only this capture's existing operations. Delayed
# completion does not itself clear Unknown or publish a second snapshot.
$script:ParentPublisherDelay=150
$fake=FakeParentCapture;$script:BuildRetentionCloseout=$null
Refuses {Complete-BuildParentCapture $fake 50} 'deadline' 'same-capture publication timeout stays unknown'
$ownedOp=$script:BuildRetentionCloseoutOperation;$ownedPublisher=$ownedOp.Publication
Check ($ownedPublisher.Task.Wait(1000) -and $null -eq $script:BuildRetentionCloseout -and $null -ne $script:BuildRetentionFailure) 'late publisher task never clears unknown automatically'
$script:ParentPublisherDelay=0
Check ((Complete-BuildParentCapture $fake 1000).ExitCode -eq 17) 'explicit same-capture call drains late publisher in fresh finite window'
Check ([object]::ReferenceEquals($ownedOp,$script:BuildRetentionCloseoutOperation) -and [object]::ReferenceEquals($ownedPublisher,$ownedOp.Publication)) 'retry reuses exact operation and original publisher'
Check ($null -eq $script:BuildRetentionFailure -and $fake.Disposals -eq 1) 'only explicit successful closeout clears unknown'
$script:ParentCompletionVerifyDelay=150
Refuses {Complete-BuildParentCapture $fake 50} 'deadline' 'completion-verifier timeout preserves its exact receipt role'
$pendingCompletionVerifier=$ownedOp.CompletionVerification
Check ($pendingCompletionVerifier.Task.Wait(1000) -and $null -ne $script:BuildRetentionFailure) 'late completion verification does not clear unknown'
$script:ParentCompletionVerifyDelay=0;$script:ParentVerificationNames.Clear()
Check ((Complete-BuildParentCapture $fake 1000).ExitCode -eq 17) 'retry drains completion verifier and freshly verifies snapshot separately'
Check ($script:ParentVerificationNames.Contains(('parent-closeout-'+$fake.DomainId+'.json'))) 'completion verification cannot substitute for required snapshot verification'
Check ($pendingCompletionVerifier.Name.EndsWith('-completion.json') -and $null -eq $ownedOp.SnapshotVerification -and $null -eq $ownedOp.CompletionVerification) 'receipt-specific verifier continuations finish without sharing task slots'

$fake=FakeParentCapture;$script:BuildRetentionCloseout=$null
[void](Complete-BuildParentCapture $fake 1000)
$script:ParentCompletionVerifyDelay=150
Refuses {Complete-BuildParentCapture $fake 50} 'deadline' 'changed-snapshot fixture first retains pending completion verification'
$op=$script:BuildRetentionCloseoutOperation
Check ($op.CompletionVerification.Task.Wait(1000)) 'changed-snapshot fixture completion task finishes independently'
$snapshotFixture=$op.Receipt.Path
$preserved=[IO.File]::Open(($snapshotFixture+'.before-corruption'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
try{$raw=[IO.File]::ReadAllBytes($snapshotFixture);$preserved.Write($raw,0,$raw.Length)}finally{$preserved.Dispose()}
# Intentional corruption of this inert mock fixture only; original bytes above
# remain preserved. No canonical accounted receipt or product file is changed.
[IO.File]::AppendAllText($snapshotFixture,"`n")
$script:ParentCompletionVerifyDelay=0
Refuses {Complete-BuildParentCapture $fake 1000} 'immutable receipt changed' 'late completion verification cannot hide a changed snapshot'
Check ($null -ne $script:BuildRetentionFailure) 'changed immutable snapshot remains unknown and cannot release'

$fake=FakeParentCapture;$script:BuildRetentionCloseout=$null;$script:ParentCompletionPublishDelay=150
Refuses {Complete-BuildParentCapture $fake 50} 'deadline' 'late completion publication stays unknown after physical disposal'
$op=$script:BuildRetentionCloseoutOperation;$completionPublisher=$op.CompletionWorker
Check ($completionPublisher.Task.Wait(1000) -and $fake.Disposals -eq 1 -and $null -eq $script:BuildRetentionCloseout) 'late completion publisher does not release or repeat disposal'
$script:ParentCompletionPublishDelay=0
Check ((Complete-BuildParentCapture $fake 1000).ExitCode -eq 17 -and [object]::ReferenceEquals($completionPublisher,$op.CompletionWorker)) 'fresh explicit retry drains original completion publication without collision'

$fake=FakeParentCapture;$fake.FailDispose=$true;$script:BuildRetentionCloseout=$null
Refuses {Complete-BuildParentCapture $fake 1000} 'synthetic stream disposal' 'published snapshot survives disposal failure'
$savedPublication=$script:BuildRetentionCloseoutOperation.Publication;$savedReceipt=$script:BuildRetentionCloseoutOperation.Receipt
$fake.FailDispose=$false
Check ((Complete-BuildParentCapture $fake 1000).ExitCode -eq 17) 'same-capture retry recovers partial disposal without immutable collision'
Check ([object]::ReferenceEquals($savedPublication,$script:BuildRetentionCloseoutOperation.Publication) -and [object]::ReferenceEquals($savedReceipt,$script:BuildRetentionCloseoutOperation.Receipt)) 'recovered disposal preserves original accounted snapshot'
$completed=Get-Content -LiteralPath $script:BuildRetentionCloseout.CompletionPath -Raw|ConvertFrom-Json
Check ($completed.diagnostics.native_cleanup_failure.Stage -eq 'close root witness' -and $completed.diagnostics.native_cleanup_failure.NativeError -eq 5) 'completion receipt preserves original cleanup stage and native code'
$fake.DomainId='foreign'
Refuses {Complete-BuildParentCapture $fake 1000} 'identity mismatch' 'foreign domain cannot adopt a retained operation'
}finally{
    Set-Item Function:Start-BuildParentReceiptWorker $realStartParentPublisher
    Set-Item Function:Read-BuildParentReceiptWorker $realReadParentPublisher
}
# Actual bounded runspace adapter and unchanged accounting backend, with fake
# capture data only. This publishes no native capture/deadline qualification.
Initialize-SmokeRunnerSupervisorType
$checks.Add('receipt supervisor managed source compiled without constructing native owner')
$fake=FakeParentCapture
$guardDescriptor=New-SmokeRunnerSupervisorDescriptor $script:BuildRetentionContext ('parent-closeout-'+$fake.DomainId+'.json') @{proof=$fake.CloseoutProof}
Refuses {Start-SmokeRunnerReceiptOperation $guardDescriptor} 'native capability is closed' 'canonical supervisor entry refuses before any native startup'
# The production parent adapter now requires a persistent supervisor. Supply an
# explicit isolated source library with inert lifetime witnesses solely for
# these actual-accounting/fake-data tests; no production gate is altered.
$script:SourceReceiptLibrary=Join-Path $evidence 'source-receipt-library'
[void][IO.Directory]::CreateDirectory($script:SourceReceiptLibrary)
$realLib=Join-Path $canonical 'tools/smoke-verify/lib'
[IO.File]::WriteAllText((Join-Path $script:SourceReceiptLibrary 'Storage-Harness.ps1'),(". '"+(Join-Path $realLib 'Storage-Harness.ps1')+"'"))
$mockWriter=@'
function Write-SmokeRunnerSupervisedJson {
 param($Context,$Name,$Value,$OwnerRecord=$null)
 $result=Write-SmokeRunnerRetainedJson $Context $Name $Value
 $result|Add-Member SupervisorContext $Context
 $result|Add-Member ParentRunId $Context.RunId
 $result|Add-Member SupervisorCloseout ([pscustomobject]@{RootExited=$true;DomainActive=0;HandlesClosed=$true;OutputMode='nul';ExitCode=0;RootPid=$Context.Owner.pid;CreationTicks=$Context.Owner.creation_ticks;source_mock_only=$true})
 return $result
}
'@
[IO.File]::WriteAllText((Join-Path $script:SourceReceiptLibrary 'Runner-Retention.ps1'),(". '"+(Join-Path $realLib 'Runner-Retention.ps1')+"'`n"+$mockWriter))
$actualStartParent=(Get-Command Start-BuildParentReceiptWorker).ScriptBlock
function Start-BuildParentReceiptWorker($Capture,$Data,[string]$Name='', $Value=$null,$ExistingReceipt=$null){
 $work=& $actualStartParent $Capture $Data $Name $Value $ExistingReceipt
 $work.Library=$script:SourceReceiptLibrary
 return $work
}
$script:BuildRetentionReceiptWorker=[pscustomobject]@{Complete=$false}
Refuses {Start-BuildParentReceiptWorker $fake ([pscustomobject]@{})} 'still owned' 'actual receipt adapter refuses a second in-progress publisher'
$script:BuildRetentionReceiptWorker=$null;$script:BuildRetentionCloseout=$null
$fake=FakeParentCapture
$actualPublication=Complete-BuildParentCapture $fake 10000
Check ($actualPublication.ExitCode -eq 17 -and $script:BuildRetentionReceiptWorker.Complete -and (Test-Path $script:BuildRetentionCloseout.Path)) 'actual runspace/accounting adapter completes fake data within finite budget'
$snapshotPath=$script:BuildRetentionCloseout.Path;$snapshotHash=(Get-FileHash -LiteralPath $snapshotPath).Hash
Check ((Complete-BuildParentCapture $fake 10000).ExitCode -eq 17 -and (Get-FileHash -LiteralPath $snapshotPath).Hash -ceq $snapshotHash) 'actual adapter verifies and reuses same immutable snapshot on explicit retry'
$fake=FakeParentCapture;$fake.Launched=$false;$fake.Id=0;$fake.CreationUtc=''
$fake.CloseoutProof.RootPid=0;$fake.CloseoutProof.CreationUtc='';$fake.CloseoutProof.ExitCode=1
$fake.LaunchFailure=[pscustomobject]@{Stage='create contained root';NativeError=5;Message='injected access denied'}
$failure=[pscustomobject]@{type='Win32Exception';message='injected access denied';native_error=5}
Check ((Complete-BuildParentCapture $fake 10000 -LaunchError $failure).ExitCode -eq 1) 'failed launch can physically close without losing original cause'
$closed=Get-Content -LiteralPath $script:BuildRetentionCloseout.Path -Raw|ConvertFrom-Json
Check ($closed.diagnostics.outcome -ceq 'no_child' -and $closed.diagnostics.native_launch_failure.Stage -ceq 'create contained root' -and $closed.diagnostics.native_launch_failure.NativeError -eq 5) 'actual accounted closed snapshot includes failed no-child stage and native code'
$done=Get-Content -LiteralPath $script:BuildRetentionCloseout.CompletionPath -Raw|ConvertFrom-Json
Check ($done.physical_disposal_complete -eq $true -and $done.diagnostics.native_launch_failure.NativeError -eq 5) 'actual completion receipt retains original launch diagnostics'
$fake=FakeParentCapture;$fake.FailDispose=$true
Refuses {Complete-BuildParentCapture $fake 10000} 'synthetic stream disposal' 'actual publication retained before disposal failure'
$originalPath=$script:BuildRetentionCloseoutOperation.Receipt.Path;$originalHash=(Get-FileHash -LiteralPath $originalPath).Hash
$fake.FailDispose=$false
Check ((Complete-BuildParentCapture $fake 10000).ExitCode -eq 17 -and (Get-FileHash -LiteralPath $originalPath).Hash -ceq $originalHash) 'actual publisher recovery leaves original snapshot immutable'
$done=Get-Content -LiteralPath $script:BuildRetentionCloseout.CompletionPath -Raw|ConvertFrom-Json
Check ($done.diagnostics.native_cleanup_failure.NativeError -eq 5) 'actual recovered completion accounts original native cleanup diagnostic'
$checks.Add('parent fake host outcomes are component evidence only, not containment/PS51/EOF native qualification')
Set-Item Function:Start-BuildParentReceiptWorker $actualStartParent

@{passed=$true;count=$checks.Count;checks=$checks.ToArray();no_C_or_Cpp_build=$true;managed_source_compilation_only=$true;no_game=$true;
    fixtures_and_archive_receipts_retained=$true;only_named_owned_protocol_releases=$true;
    isolated_full_wrapper_probes=$probeResults.ToArray();PowerShellVersion=$PSVersionTable.PSVersion.ToString();PowerShellEdition=$PSVersionTable.PSEdition;
    host_source_checks_executed=$true;native_supervisor_job_IPC_qualified=$false;
    historical_PS51_denial='Preserved batch1 AuthorizationManager failure; current -File admitted through normal tool review, no execution-policy/security change'}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $evidence 'result.json') -Encoding utf8
Write-Output "PASS $($checks.Count) build wrapper retention checks; managed source compilation only, no C/C++ build or game."
