#Requires -Version 5.1
# Immediate native identity and actual runner failure blocks; hidden tiny actors only.
param([Parameter(Mandatory)][string]$EvidenceDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'lib/Owned-Process.ps1')
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
. (Join-Path $PSScriptRoot 'lib/Console-Capture.ps1')
. (Join-Path $PSScriptRoot 'lib/Owned-Process-Cue.ps1')
if(Test-Path -LiteralPath $EvidenceDirectory){throw 'Fresh retained test evidence required'}
$EvidenceDirectory=[IO.Path]::GetFullPath($EvidenceDirectory)
[void][IO.Directory]::CreateDirectory($EvidenceDirectory)
$checks=New-Object 'Collections.Generic.List[string]'
$children=New-Object 'Collections.Generic.List[Diagnostics.Process]'
$immediate=New-Object 'Collections.Generic.List[object]'
$failures=New-Object 'Collections.Generic.List[object]'
$contexts=New-Object 'Collections.Generic.List[object]'
function Check([bool]$Condition,[string]$Name){if(!$Condition){throw ('FAIL: '+$Name)};$checks.Add($Name)}
function Refuses([scriptblock]$Action,[string]$Message){$caught=$false;try{& $Action|Out-Null}catch{if($_.Exception.ToString() -notlike ('*'+$Message+'*')){throw};$caught=$true};Check $caught ('refuses: '+$Message)}
function Copy-Record($Value){return ($Value|ConvertTo-Json -Depth 8|ConvertFrom-Json)}
function Write-Info($Message){}
function Write-Fail($Message){}
$consumer=Join-Path $EvidenceDirectory 'tiny-consumer.exe'
$compiler=Join-Path $EvidenceDirectory 'compile-fixture.ps1'
[IO.File]::WriteAllText($compiler,@'
param([string]$Output)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition 'public static class StartupFixture { public static void Main(string[] args) { if(args.Length>0 && args[0]=="exit-immediately") return; System.Console.WriteLine("startup fixture"); System.Console.ReadLine(); } }' -OutputAssembly $Output -OutputType ConsoleApplication
'@,(New-Object Text.UTF8Encoding($false)))
$compileStart=New-Object Diagnostics.ProcessStartInfo
$compileStart.FileName='C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
$compileStart.Arguments='-NoProfile -NonInteractive -File "'+$compiler+'" "'+$consumer+'"'
$compileStart.UseShellExecute=$false;$compileStart.CreateNoWindow=$true
$compileProcess=[Diagnostics.Process]::Start($compileStart)
try{if(!$compileProcess.WaitForExit(15000) -or $compileProcess.ExitCode){throw 'Tiny test compilation failed'}}
finally{if(!$compileProcess.HasExited){[void](Stop-SmokeRetainedProcess $compileProcess)};$compileProcess.Dispose()}
$consumerHash=(Get-FileHash $consumer).Hash
Initialize-SmokeProcessHandleReader
$runnerText=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'run.ps1'))
$tokens=$null;$parseErrors=$null
$runnerAst=[Management.Automation.Language.Parser]::ParseInput($runnerText,[ref]$tokens,[ref]$parseErrors)
Check (@($parseErrors).Count -eq 0) 'runner parses'
foreach($functionName in @('New-SmokeRetainedLaunch','Close-SmokeRetainedLaunch','Get-SmokeLaunchCloseoutSummary','Stop-SmokeOwnedFaultProcesses')){
    $ast=$runnerAst.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName},$true)
    . ([scriptblock]::Create($ast.Extent.Text))
}
$script:SmokeRetainedLaunches=@{};$script:SmokeRetainedFaults=@();$script:SmokeOwnedProcesses=@{};$script:SmokeOwnedPids=@()
$script:FixtureCaptures=@{}
function Start-Tiny([string]$Token){
    $start=New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$consumer;$start.Arguments=$Token;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $child=[Diagnostics.Process]::Start($start);$children.Add($child)
    $script:FixtureCaptures[$child.Id]=Start-SmokeConsoleCapture -Process $child -Directory $EvidenceDirectory
    return $child
}
$originalOwnership=(Get-Item Function:New-SmokeProcessOwnership).ScriptBlock
try{
    for($ordinal=1;$ordinal -le 20;$ordinal++){
        $token='immediate-'+[guid]::NewGuid().ToString('N')
        $psi=New-Object Diagnostics.ProcessStartInfo
        $psi.FileName=$consumer;$psi.Arguments=$token;$psi.UseShellExecute=$false;$psi.CreateNoWindow=$true
        $psi.RedirectStandardInput=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $child=[Diagnostics.Process]::Start($psi)
        try{$owned=New-SmokeProcessOwnership -Process $child -ExpectedExecutable $consumer -CommandLineToken $token}
        finally{$children.Add($child)}
        $watch.Stop()
        $capture=Start-SmokeConsoleCapture -Process $child -Directory $EvidenceDirectory
        $info=Get-CimInstance Win32_Process -Filter ("ProcessId = {0}" -f $child.Id) -ErrorAction Stop
        Check ((Test-SmokeProcessOwnership $owned $info).status -eq 'matched') ('immediate '+$ordinal+' independent CIM path/PID/creation/parent/token agreement')
        $stopped=Stop-SmokeOwnedProcess $owned
        $drain=Complete-SmokeConsoleCapture $capture
        Check ($stopped.status -eq 'exited' -and $child.HasExited -and $drain.Passed) ('immediate '+$ordinal+' exact absence and pipes')
        $immediate.Add([pscustomobject]@{attempt=$ordinal;elapsed_ms=$watch.Elapsed.TotalMilliseconds;ownership=$owned;independent_cim=($info|Select-Object ProcessId,ExecutablePath,CreationDate,ParentProcessId,CommandLine);closeout=$stopped;console=$drain})
    }
    $token='negative-'+[guid]::NewGuid().ToString('N');$child=Start-Tiny $token
    $owned=New-SmokeProcessOwnership $child $consumer $token
    foreach($negative in @(@{field='executable_path';value='C:/unrelated/other.exe';reason='Executable mismatch'},@{field='command_line_token';value='other-token';reason='token mismatch'},@{field='parent_process_id';value=($PID+1);reason='Parent mismatch'},@{field='process_id';value=$PID;reason='self-targeted'},@{field='creation_utc';value=[DateTimeOffset]::UtcNow.AddDays(-1).ToString('o');reason='Creation time mismatch'})){
        $wrong=Copy-Record $owned;$wrong.($negative.field)=$negative.value
        Refuses {Stop-SmokeOwnedProcess $wrong} $negative.reason
        Check (!$child.HasExited) ('negative '+$negative.field+' preserves actor')
    }
    Refuses {New-SmokeProcessOwnership $child 'C:/wrong.exe' $token} 'identity mismatch'
    Refuses {[PdSmokeProcessHandleIdentity]::ReadNative($child.Handle,($child.Id+1))} 'PID mismatch'
    Refuses {[PdSmokeProcessHandleIdentity]::ReadNative([IntPtr]::Zero,$child.Id)} 'Invalid retained'
    $file=[IO.File]::OpenRead($consumer)
    try{Refuses {[PdSmokeProcessHandleIdentity]::ReadNative($file.SafeFileHandle.DangerousGetHandle(),$child.Id)} 'PID query failed'}finally{$file.Dispose()}
    $disposed=New-Object Diagnostics.Process;$disposed.Dispose()
    Refuses {Get-SmokeProcessHandleIdentity $disposed} 'process'
    if(Test-Path Function:Get-CimInstance){throw 'Unexpected CIM function shadow'}
    try{function Get-CimInstance {[CmdletBinding()]param($ClassName,$Filter)throw 'forced metadata query failure'};Refuses {Stop-SmokeOwnedProcess $owned} 'forced metadata query failure'}
    finally{Remove-Item Function:Get-CimInstance}
    Check (!$child.HasExited) 'metadata query failure does not select or terminate actor'
    [void](Stop-SmokeRetainedProcess $child)
    $early=Start-Tiny 'exit-immediately'
    Check ($early.WaitForExit(5000)) 'early exit completed'
    Refuses {New-SmokeProcessOwnership $early $consumer 'exit-immediately'} 'has exited'
    Refuses {Get-SmokeProcessHandleIdentity $early} 'has exited'

    # Execute actual canonical registration/publication statements and catch,
    # with a hidden tiny ProcessStartInfo and a forced ownership failure.
    $mainTry=$runnerAst.FindAll({param($node)$node -is [Management.Automation.Language.TryStatementAst] -and $node.Body.Extent.Text.Contains('$ownership = New-SmokeProcessOwnership -Process $proc')},$true)|Sort-Object {$_.Extent.Text.Length}|Select-Object -First 1
    $start=$runnerText.IndexOf('        $proc = [System.Diagnostics.Process]::Start($psi)')
    $end=$runnerText.IndexOf('        if ($runtimeStrategy -eq "timeout-kill")',$start)
    $mainLaunch=[scriptblock]::Create($runnerText.Substring($start,$end-$start))
    $catchText=$mainTry.CatchClauses[0].Body.Extent.Text
    $mainCatch=[scriptblock]::Create($catchText.Substring(1,$catchText.Length-2))
    $initStart=$runnerText.LastIndexOf('    $proc = $null',$mainTry.Extent.StartOffset)
    $mainInit=[scriptblock]::Create($runnerText.Substring($initStart,$mainTry.Extent.StartOffset-$initStart))
    $multiTry=$runnerAst.FindAll({param($node)$node -is [Management.Automation.Language.TryStatementAst] -and $node.Body.Extent.Text.Contains('$ownership = New-SmokeProcessOwnership -Process $p ')},$true)|Sort-Object {$_.Extent.Text.Length}|Select-Object -First 1
    Check ($null -ne $multiTry) 'actual multi registration try found'
    $multiLaunch=[scriptblock]::Create($multiTry.Extent.Text)
    $initStart=$runnerText.LastIndexOf('        $p = $null',$multiTry.Extent.StartOffset)
    $multiInit=[scriptblock]::Create($runnerText.Substring($initStart,$multiTry.Extent.StartOffset-$initStart))
    $multiClose=$runnerAst.FindAll({param($node)$node -is [Management.Automation.Language.IfStatementAst] -and $node.Extent.Text.Contains('Close-SmokeRetainedLaunch -Launch $entry.RetainedLaunch')},$true)|Sort-Object {$_.Extent.Text.Length}|Select-Object -First 1
    $closePeers=[scriptblock]::Create($multiClose.Extent.Text)
    $reuseGate=$runnerAst.FindAll({param($node)$node -is [Management.Automation.Language.IfStatementAst] -and $node.Extent.Text.Contains('Complete-SmokeReadOnlyProfile')},$true)|Sort-Object {$_.Extent.Text.Length}|Select-Object -First 1
    $profileCloseout=[scriptblock]::Create($reuseGate.Extent.Text)
    $verdictGate=$runnerAst.Find({param($node)$node -is [Management.Automation.Language.IfStatementAst] -and $node.Extent.Text.StartsWith('if ($launchFailure)')},$true)
    $failureVerdict=[scriptblock]::Create($verdictGate.Extent.Text)
    $unrelated=Start-Tiny ('unrelated-'+[guid]::NewGuid().ToString('N'))
    foreach($denyQuery in @($false,$true)){
        $ownership='stale ownership from prior launch'
        . $mainInit
        Check (!$ownership -and !$launch -and !$proc) 'actual single caller clears prior launch ownership and retained state'
        $cueContext=Open-SmokeOwnedProcessCueContext -ProjectRoot $root -Directory (Join-Path $root ('.claude/smoke-process-cues/codex-pd-local-cue-20261010/startup-'+[guid]::NewGuid().ToString('N'))) -SessionId 'codex-pd-local-cue-20261010' -ExpiresUtc ([DateTimeOffset]::UtcNow.AddMinutes(10).ToString('o')) -TimeoutSeconds 5
        $contexts.Add($cueContext)
        $installInfo=[pscustomobject]@{InstallDir=$EvidenceDirectory};$exe=$consumer;$exeLeaf='tiny-consumer.exe';$Test=[pscustomobject]@{Path='forced-single'}
        $OwnedProcessCueDirectory=$cueContext.Directory;$cueFailure=$null;$cueCloseoutError=$null;$ownership=$null;$launch=$null;$consoleCapture=$null
        $psi.Arguments=$Test.Path
        function New-SmokeProcessOwnership {throw 'forced registration failure'}
        if($denyQuery){function Get-CimInstance {[CmdletBinding()]param($ClassName,$Filter)throw 'forced cleanup query failure'}}
        $closeError=$null
        try{. $mainLaunch}catch{try{. $mainCatch}catch{$closeError=$_.Exception.ToString()}}
        $children.Add($proc)
        if($denyQuery){Remove-Item Function:Get-CimInstance}
        Check ($proc.HasExited -and $launch.ConsoleResult.Passed -and !$ownership) 'single registration failure closes actual retained child and drains pipes without fabricated ownership'
        Check (Test-Path (Join-Path $cueContext.Directory 'cancelled.json')) 'single failure cue cancellation retained'
        Check (!$unrelated.HasExited) 'single failure preserves unrelated same-executable actor'
        if($denyQuery){
            Check (!$launch.Verified -and $closeError -like '*forced registration failure*' -and $closeError -like '*forced cleanup query failure*') 'original plus cleanup error and unverified lease gate preserved'
            $ReuseInstallId='retained-lease-test';$script:ProfileCompletions=0
            function Complete-SmokeReadOnlyProfile {$script:ProfileCompletions++}
            Refuses {. $profileCloseout} 'closeout unverified'
            Check ($script:ProfileCompletions -eq 0) 'actual production profile gate leaves lease completion unreached'
            Remove-Item Function:Complete-SmokeReadOnlyProfile
            [void](Close-SmokeRetainedLaunch $launch)
            Check ($launch.Verified -and $launch.CleanupErrors.Count -gt 0) 'verified retry retains earlier failed cleanup evidence'
        }else{Check ($exitCode -eq -3 -and $launch.Verified -and $cueFailure -like '*forced registration failure*') 'single forced failure retains failure verdict and verified closeout'}
        $cueFailure=$null;$testOk=$true;$exitCode=0
        . $failureVerdict
        Check (!$testOk) 'actual failure verdict survives clean native sentinel and absent cue failure flag'
        $failures.Add((Get-SmokeLaunchCloseoutSummary $launch))
        Set-Item Function:New-SmokeProcessOwnership -Value $originalOwnership
    }
    $procs=@();$retainedForTest=@();$launchFailed=$false;$processExe=$consumer;$processSmokePath='forced-multi';$processInstallInfo=[pscustomobject]@{InstallDir=$EvidenceDirectory}
    foreach($ordinal in @(1,2)){
        $ownership='stale ownership from prior launch'
        . $multiInit
        Check (!$ownership -and !$launch -and !$p) 'actual multi caller clears prior launch ownership and retained state'
        $psi.Arguments=$processSmokePath
        if($ordinal -eq 2){function New-SmokeProcessOwnership {throw 'forced second registration failure'}}
        . $multiLaunch
        $procs += [pscustomobject]@{Process=$p;RetainedLaunch=$launch}
        $children.Add($p)
    }
    Set-Item Function:New-SmokeProcessOwnership -Value $originalOwnership
    if($p -and $p -notin @($children)){$children.Add($p)}
    . $closePeers
    Check ($launchFailed -and $procs.Count -eq 1 -and $retainedForTest.Count -eq 2) 'production multi second registration failure retained independently'
    Check (@($retainedForTest|Where-Object {!$_.Verified -or !$_.Process.HasExited -or !$_.ConsoleResult.Passed}).Count -eq 0) 'multi failed child and earlier sibling bounded closeout and pipes'
    Check (!$unrelated.HasExited) 'multi failure preserves unrelated same-executable actor'
    foreach($retained in $retainedForTest){$failures.Add((Get-SmokeLaunchCloseoutSummary $retained))}

    # Related-reporter route uses tiny actors and mocked candidate metadata,
    # never creates or queries a real OS crash reporter.
    $anchor=Start-Tiny 'reporter-anchor';$anchorOwner=New-SmokeProcessOwnership $anchor $consumer 'reporter-anchor'
    $script:SmokeOwnedProcesses[$anchor.Id]=$anchorOwner
    $reporter=Start-Tiny 'reporter-fixture'
    $script:ReporterCandidate=Get-CimInstance Win32_Process -Filter ("ProcessId = {0}" -f $reporter.Id)|Select-Object ProcessId,ExecutablePath,CreationDate,ParentProcessId,CommandLine,Name
    $script:ReporterCandidate.Name='WerFault.exe';$script:ReporterCandidate.ParentProcessId=$anchor.Id
    function Get-CimInstance {[CmdletBinding()]param($ClassName,$Filter)if($Filter -eq "name = 'WerFault.exe'"){return $script:ReporterCandidate};return CimCmdlets\Get-CimInstance -ClassName $ClassName -Filter $Filter -ErrorAction Stop}
    function New-SmokeProcessOwnership {throw 'forced reporter registration failure'}
    Refuses {Stop-SmokeOwnedFaultProcesses -KnownPids @($anchor.Id)} 'forced reporter registration failure'
    Remove-Item Function:Get-CimInstance
    Set-Item Function:New-SmokeProcessOwnership -Value $originalOwnership
    Check ($reporter.HasExited -and !$unrelated.HasExited -and $script:SmokeRetainedFaults[-1].Verified) 'actual reporter failure catch closes only independently bound tiny candidate'
    $unrelated.StandardInput.Close();Check ($unrelated.WaitForExit(5000)) 'unrelated actor exits cooperatively'
    $actorCloseouts=@()
    foreach($child in $children){$actorCloseouts+=Stop-SmokeRetainedProcess $child}
    foreach($capture in $script:FixtureCaptures.Values){Check ((Complete-SmokeConsoleCapture $capture).Passed) 'remaining tiny fixture pipes drained'}
    $result=[ordered]@{verdict='pass';checks=$checks.Count;names=$checks.ToArray();powershell=$PSVersionTable.PSVersion.ToString();observed_utc=[DateTime]::UtcNow.ToString('o');consumer=$consumer;consumer_sha256=$consumerHash;immediate_launches=$immediate.ToArray();production_failure_closeouts=$failures.ToArray();test_actor_closeouts=$actorCloseouts;owned_actors_remaining=0;game_launches=0;controller_actions=0;crash_reporter_provoked=$false;deletion_performed=$false}
    [IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'result.json'),($result|ConvertTo-Json -Depth 12),(New-Object Text.UTF8Encoding($false)))
    [pscustomobject]@{verdict='pass';checks=$checks.Count;powershell=$result.powershell;immediate_launches=$immediate.Count;owned_actors_remaining=0}|ConvertTo-Json -Compress
}finally{
    if(Test-Path Function:Get-CimInstance){Remove-Item Function:Get-CimInstance}
    Set-Item Function:New-SmokeProcessOwnership -Value $originalOwnership
    foreach($child in $children){try{if(!$child.HasExited){[void](Stop-SmokeRetainedProcess $child)}}finally{$child.Dispose()}}
    foreach($capture in $script:FixtureCaptures.Values){[void](Complete-SmokeConsoleCapture $capture)}
    foreach($context in $contexts){$context.Guard.Dispose()}
}
