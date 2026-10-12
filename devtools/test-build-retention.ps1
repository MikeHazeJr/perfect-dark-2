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
$ast=[Management.Automation.Language.Parser]::ParseFile($wrapper,[ref]$tokens,[ref]$errors)
Check (@($errors).Count -eq 0) 'wrapper parse'
$old=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($BeforeWrapper),[ref]$tokens,[ref]$errors)
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
$wrapperText=[IO.File]::ReadAllText($wrapper)
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
foreach($edit in $edits|Sort-Object Start -Descending){$instrumented=$instrumented.Remove($edit.Start,$edit.Length).Insert($edit.Start,$edit.Text)}
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
    [IO.File]::WriteAllText($copy,$instrumented)
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

@{passed=$true;count=$checks.Count;checks=$checks.ToArray();no_native_compiler=$true;no_game=$true;
    fixtures_and_archive_receipts_retained=$true;only_named_owned_protocol_releases=$true;
    isolated_full_wrapper_probes=$probeResults.ToArray();PS7=$PSVersionTable.PSVersion.ToString();
    PS51='blocked/unretried; AST here is PS7 only; v1 incident ran normal existing PS51 builder'}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $evidence 'result.json') -Encoding utf8
Write-Output "PASS $($checks.Count) build wrapper retention checks; no native compiler or game."
