#Requires -Version 5.1
# Retained tiny fixtures and hidden shells only; no game or filesystem deletion.
param([Parameter(Mandatory)][string]$EvidenceDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Owned-Process.ps1')
. (Join-Path $PSScriptRoot 'lib/Reuse-Runtime.ps1')
. (Join-Path $PSScriptRoot 'lib/Owned-Process-Cue.ps1')
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
. (Join-Path $PSScriptRoot 'lib/Console-Capture.ps1')
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (Test-Path -LiteralPath $EvidenceDirectory) { throw 'Test evidence must be fresh' }
[void][IO.Directory]::CreateDirectory($EvidenceDirectory)
$script:checks = New-Object 'Collections.Generic.List[string]'
function Check([bool]$Value, [string]$Name) {
    if (!$Value) { throw "FAIL: $Name" }; $script:checks.Add($Name)
}
function Refuses([scriptblock]$Action, [string]$Message) {
    $failed=$false
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notlike ('*'+$Message+'*')) { throw }
        $failed=$true
    }
    Check $failed ('refuses: '+$Message)
}
function Copy-Value($Value) { $Value | ConvertTo-Json -Depth 12 | ConvertFrom-Json }
$fixturePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'tests/menu_virtual_controller_agent_cancel.json'))
$fixtureHash = (Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash.ToLowerInvariant()
# System PowerShell.exe is an OS hardlink; it correctly fails consumer admission.
# Compile a small disposable .NET Framework console fixture using PS5.1, rather
# than weakening the production hardlink check or copying a real game binary.
$hostPath = Join-Path ([IO.Path]::GetFullPath($EvidenceDirectory)) 'tiny-consumer.exe'
$compiler = Join-Path ([IO.Path]::GetFullPath($EvidenceDirectory)) 'compile-fixture.ps1'
[IO.File]::WriteAllText($compiler, @'
param([string]$Output)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition 'public static class OwnedCueFixture { public static void Main(string[] args) { System.Console.WriteLine("owned fixture"); System.Console.ReadLine(); } }' -OutputAssembly $Output -OutputType ConsoleApplication
'@, (New-Object Text.UTF8Encoding($false)))
$compilerStart=New-Object Diagnostics.ProcessStartInfo
$compilerStart.FileName='C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
$compilerStart.Arguments='-NoProfile -NonInteractive -File "'+$compiler+'" "'+$hostPath+'"'
$compilerStart.UseShellExecute=$false; $compilerStart.CreateNoWindow=$true
$compilerProcess=[Diagnostics.Process]::Start($compilerStart)
try { if (!$compilerProcess.WaitForExit(15000) -or $compilerProcess.ExitCode -ne 0) { throw 'Tiny consumer compilation failed' } }
finally { if (!$compilerProcess.HasExited) { $compilerProcess.Kill(); [void]$compilerProcess.WaitForExit(5000) }; $compilerProcess.Dispose() }
$hostHash = (Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant()
$python = Get-SmokeStoragePython
$fixture = & $python -B (Join-Path $PSScriptRoot 'test-storage-reuse.py') --fixture | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Tiny split-layout fixture setup failed' }
$script:SmokeStoragePolicy = @{schema=1;min_free_gib=0;max_full_installs=1;max_full_install_gib=65536/1GB;
    max_archive_gib=0.004;peak_run_gib=1024/1GB;extra_growth_gib=0;peak_build_gib=0}
$profile = New-SmokeReadOnlyProfile -ProjectRoot $fixture.project -InstallId $fixture.id -Seed $fixture.seed_id -BinarySha256 $fixture.binary_sha256
$installInfo = [pscustomobject]@{BaseDir=$profile.InstallDir;InstallDir=(Split-Path -Parent $profile.ProfileDir);
    ProfileDir=$profile.ProfileDir;ReuseId=$profile.ReuseId}
$binding = [pscustomobject]@{consumer_path=$hostPath;consumer_sha256=$hostHash;fixture_path=$fixturePath;
    fixture_sha256=$fixtureHash;fixture_name='menu_virtual_controller_agent_cancel';base_install_id=$fixture.id;
    seed_id=$fixture.seed_id;base_binary_sha256=$fixture.binary_sha256;base_directory=$installInfo.BaseDir;
    runtime_root=$installInfo.InstallDir;profile_directory=$profile.ProfileDir;reuse_id=$profile.ReuseId}
$helper = Join-Path ([IO.Path]::GetFullPath($EvidenceDirectory)) 'waiting-child.ps1'
[IO.File]::WriteAllText($helper,'param([string]$Fixture); [Console]::WriteLine("owned fixture"); [void][Console]::ReadLine()', (New-Object Text.UTF8Encoding($false)))
$session = 'codex-pd-local-cue-20261010'
$contexts = @(); $children = @(); $ownerships = @(); $leaseClosed=$false
$consumerLock = Open-SmokeReuseConsumer -Path $hostPath -Sha256 $hostHash
$fixtureLock = Open-SmokeReuseConsumer -Path $fixturePath -Sha256 $fixtureHash
function Fresh-Context {
    $directory = Join-Path $root ('.claude/smoke-process-cues/'+$session+'/test-'+[guid]::NewGuid().ToString('N'))
    Open-SmokeOwnedProcessCueContext -ProjectRoot $root -Directory $directory -SessionId $session `
        -ExpiresUtc ([DateTimeOffset]::UtcNow.AddMinutes(10).ToString('o')) -TimeoutSeconds 5
}
function Write-TestCue($Context,$Record) {
    [IO.File]::WriteAllText((Join-Path $Context.Directory 'owned-process.json'), ($Record|ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding($false)))
}
try {
    $psi=New-Object Diagnostics.ProcessStartInfo
    $psi.FileName=$hostPath; $psi.Arguments='"'+$fixturePath+'"'
    $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true; $psi.RedirectStandardInput=$true
    $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true
    $child=[Diagnostics.Process]::Start($psi); $children+= $child
    Check (!$child.WaitForExit(300)) 'first hidden fixture startup'
    $owned=New-SmokeProcessOwnership -Process $child -ExpectedExecutable $hostPath -CommandLineToken $fixturePath
    $ownerships+= $owned
    Check (!$child.WaitForExit(400)) 'hidden owned child remains alive'
    $context=Fresh-Context; $contexts+= $context
    $publish=@{Context=$context;Ownership=$owned;Binding=$binding;ConsumerLock=$consumerLock;FixtureLock=$fixtureLock}
    $cuePath=Publish-SmokeOwnedProcessCue @publish
    $read=@{Directory=$context.Directory;ExpectedContext=$context.Identity;ExpectedBinding=$binding}
    $cue=Read-SmokeOwnedProcessCue @read
    Check ($cue.actor.process_id -eq $child.Id -and $cue.meaning -eq 'owned_process_only') 'live split-layout cue revalidated'
    Check ($cue.binding.base_directory -ne $cue.binding.runtime_root -and $cue.binding.consumer_path -ne (Join-Path $cue.binding.base_directory 'PerfectDark.exe')) 'base, external consumer and private output remain distinct'
    $before=(Get-FileHash -LiteralPath $cuePath -Algorithm SHA256).Hash
    Refuses { Publish-SmokeOwnedProcessCue @publish } 'already exists'
    Check ((Get-FileHash -LiteralPath $cuePath -Algorithm SHA256).Hash -eq $before) 'stale cue bytes preserved'
    Refuses { Open-SmokeOwnedProcessCueContext -ProjectRoot $root -Directory $context.Directory -SessionId $session -ExpiresUtc ([DateTimeOffset]::UtcNow.AddMinutes(10).ToString('o')) -TimeoutSeconds 5 } 'fresh'
    foreach ($field in @('consumer_sha256','fixture_sha256','seed_id','base_binary_sha256','reuse_id','profile_directory','fixture_name')) {
        $bad=Copy-Value $binding
        if ($field -like '*sha256' -or $field -eq 'seed_id') { $bad.$field='0'*64 }
        elseif ($field -eq 'profile_directory') { $bad.$field=Join-Path $bad.runtime_root 'wrong-profile' }
        else { $bad.$field='wrong-identity' }
        $badRead=@{Directory=$context.Directory;ExpectedContext=$context.Identity;ExpectedBinding=$bad}
        Refuses { Read-SmokeOwnedProcessCue @badRead } $(if ($field -in @('consumer_sha256','fixture_sha256')) {'SHA256 mismatch'} else {'mismatch'})
    }
    foreach ($field in @('process_id','creation_utc','parent_process_id','command_line_token')) {
        $bad=Copy-Value $cue
        switch ($field) {
            'process_id' { $bad.actor.process_id=$PID }
            'creation_utc' { $bad.actor.creation_utc=(ConvertTo-SmokeProcessUtc $bad.actor.creation_utc).AddSeconds(4).ToString('o') }
            'parent_process_id' { $bad.actor.parent_process_id=1 }
            'command_line_token' { $bad.actor.command_line_token=Join-Path $root 'wrong-fixture.json' }
        }
        Write-TestCue $context $bad
        Refuses { Read-SmokeOwnedProcessCue @read } $(if ($field -eq 'process_id') {'self-targeted'} elseif ($field -eq 'creation_utc') {'Creation time mismatch'} else {'binding mismatch'})
        Write-TestCue $context $cue
    }
    $bad=Copy-Value $context.Identity; $bad.run_id='stale-context'
    Refuses { Read-SmokeOwnedProcessCue -Directory $context.Directory -ExpectedContext $bad -ExpectedBinding $binding } 'context mismatch'
    $expired=Fresh-Context; $contexts+=$expired
    $expired.Identity.started_utc=[DateTimeOffset]::UtcNow.AddSeconds(-10).ToString('o')
    $expired.Identity.expires_utc=[DateTimeOffset]::UtcNow.AddSeconds(-1).ToString('o')
    Refuses { Publish-SmokeOwnedProcessCue -Context $expired -Ownership $owned -Binding $binding -ConsumerLock $consumerLock -FixtureLock $fixtureLock } 'expired'
    [IO.File]::WriteAllText((Join-Path $expired.Directory 'context.json'),($expired.Identity|ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($false)))
    $expiredCue=Copy-Value $cue; $expiredCue.context=$expired.Identity
    Write-TestCue $expired $expiredCue
    Refuses { Read-SmokeOwnedProcessCue -Directory $expired.Directory -ExpectedContext $expired.Identity -ExpectedBinding $binding } 'expired'
    Refuses { Open-SmokeOwnedProcessCueContext -ProjectRoot $root -Directory (Join-Path $root '.claude/smoke-process-cues/escape') -SessionId $session -ExpiresUtc ([DateTimeOffset]::UtcNow.AddMinutes(10).ToString('o')) -TimeoutSeconds 5 } 'session-owned'
    Refuses { ConvertTo-SmokeCuePath ($context.Directory+'\..\escape') } 'alias'
    Refuses { ConvertTo-SmokeCuePath ($context.Directory+':stream') } 'alias'
    Refuses { Open-SmokeOwnedProcessCueContext -ProjectRoot $root -Directory ($context.Directory+'new') -SessionId $session -ExpiresUtc ([DateTimeOffset]::UtcNow.AddMinutes(16).ToString('o')) -TimeoutSeconds 5 } 'bounded'

    # Exercise the actual canonical runner publication statements with a real
    # launched child, actual locked files, profile lease and real pipe drains.
    $runnerText=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'run.ps1'))
    $begin=$runnerText.IndexOf('        $ownership = New-SmokeProcessOwnership -Process $proc')
    $end=$runnerText.IndexOf('        if ($runtimeStrategy -eq "timeout-kill")',$begin)
    Check ($begin -gt 0 -and $end -gt $begin) 'production publication precedes readiness/wait branch'
    $launchBlock=[scriptblock]::Create($runnerText.Substring($begin,$end-$begin))
    $tokens=$null; $parseErrors=$null
    $runnerAst=[Management.Automation.Language.Parser]::ParseInput($runnerText,[ref]$tokens,[ref]$parseErrors)
    $launchTry=$runnerAst.FindAll({param($node) $node -is [Management.Automation.Language.TryStatementAst] -and
        $node.Body.Extent.Text.Contains('$ownership = New-SmokeProcessOwnership -Process $proc')},$true) |
        Sort-Object { $_.Extent.Text.Length } | Select-Object -First 1
    $catchText=$launchTry.CatchClauses[0].Body.Extent.Text
    $runnerCatch=[scriptblock]::Create($catchText.Substring(1,$catchText.Length-2))
    function Write-Info($Message) {}
    function Write-Fail($Message) {}
    $proc=$child; $exe=$hostPath; $Test=[pscustomobject]@{Path=$fixturePath}
    $script:SmokeOwnedProcesses=@{}; $script:SmokeOwnedPids=@()
    $script:ReuseConsumerLock=$consumerLock; $script:ReuseDefinitionLock=$fixtureLock
    $ReuseConsumerSha256=$hostHash; $originalFixtureHash=$fixtureHash; $name=$binding.fixture_name
    $ReuseInstallId=$fixture.id; $ReuseSeed=$fixture.seed_id; $ReuseBinarySha256=$fixture.binary_sha256
    $cueContext=$null; $consoleCapture=$null
    . $launchBlock
    Check ($null -eq $cueContext -and (Get-SmokeOwnedProcessState $ownership).status -eq 'matched') 'absent cue option executes unchanged production ownership path'
    [void](Stop-SmokeOwnedProcess $owned)
    Check ((Complete-SmokeConsoleCapture -Capture $consoleCapture).Passed) 'absent option console drained'
    $child=[Diagnostics.Process]::Start($psi); $children+=$child
    Check (!$child.WaitForExit(300)) 'optional cue hidden fixture startup'
    $owned=New-SmokeProcessOwnership -Process $child -ExpectedExecutable $hostPath -CommandLineToken $fixturePath
    $ownerships+=$owned; $proc=$child
    $live=Fresh-Context; $contexts+=$live; $cueContext=$live
    . $launchBlock
    $liveCue=Read-SmokeOwnedProcessCue -Directory $live.Directory -ExpectedContext $live.Identity -ExpectedBinding $binding
    Check ($liveCue.actor.process_id -eq $child.Id) 'production optional cue publishes live split-layout actor'
    [void](Stop-SmokeOwnedProcess $owned)
    Check ((Complete-SmokeConsoleCapture -Capture $consoleCapture).Passed) 'successful publication console drained'
    Close-SmokeOwnedProcessCueContext $live
    Refuses { Read-SmokeOwnedProcessCue -Directory $live.Directory -ExpectedContext $live.Identity -ExpectedBinding $binding } 'closed'
    $child=[Diagnostics.Process]::Start($psi); $children+=$child
    Check (!$child.WaitForExit(300)) 'output-failure hidden fixture startup'
    $owned=New-SmokeProcessOwnership -Process $child -ExpectedExecutable $hostPath -CommandLineToken $fixturePath
    $ownerships+=$owned; $proc=$child
    # Output failure after launch: use an occupied destination, preserving it.
    $failed=Fresh-Context; $contexts+=$failed
    $blocked=Join-Path $failed.Directory 'owned-process.json'
    [IO.File]::WriteAllText($blocked,'stale destination retained')
    $cueContext=$failed
    $OwnedProcessCueDirectory=$failed.Directory; $cueCloseoutError=$null; $cueFailure=$null; $exeLeaf='tiny-consumer.exe'
    $publicationFailed=$false
    try { . $launchBlock } catch {
        Check ($_.Exception.Message -like '*already exists*') 'production publication failed after actual launch'
        . $runnerCatch
        $publicationFailed=$true
    }
    Check ($publicationFailed -and $exitCode -eq -3 -and [bool]$cueFailure) 'production catch preserves cue failure verdict'
    $stopped=Stop-SmokeOwnedProcess -Ownership $owned
    $consoleResult=Complete-SmokeConsoleCapture -Capture $consoleCapture
    Check ($stopped.status -eq 'exited' -and $consoleResult.Passed) 'publication failure drains console and verifies exact actor absence'
    Check ([IO.File]::ReadAllText($blocked) -eq 'stale destination retained' -and (Test-Path -LiteralPath (Join-Path $failed.Directory 'cancelled.json'))) 'failed output and cancellation evidence retained'
    Refuses { Read-SmokeOwnedProcessCue @read } 'exited'
    $early=Fresh-Context; $contexts+=$early
    Refuses { Publish-SmokeOwnedProcessCue -Context $early -Ownership $owned -Binding $binding -ConsumerLock $consumerLock -FixtureLock $fixtureLock } 'exited'
    Close-SmokeOwnedProcessCueContext -Context $context -Cancelled
    Refuses { Read-SmokeOwnedProcessCue @read } 'cancelled'
    Refuses { Publish-SmokeOwnedProcessCue @publish } 'cancelled'
    Close-SmokeOwnedProcessCueContext -Context $early
    Check (Test-Path -LiteralPath (Join-Path $early.Directory 'closed.json')) 'normal closeout marker retained'
    # Junction and hardlink fixtures are retained, including their targets.
    $junction=Join-Path $EvidenceDirectory 'cue-junction'
    [void](New-Item -ItemType Junction -Path $junction -Value $context.Directory)
    Refuses { Read-SmokeOwnedProcessCue -Directory ([IO.Path]::GetFullPath($junction)) -ExpectedContext $context.Identity -ExpectedBinding $binding } 'alias/reparse'
    $aliasSession=$session+'-alias-'+[guid]::NewGuid().ToString('N').Substring(0,8)
    $writerJunction=Join-Path $root ('.claude/smoke-process-cues/'+$aliasSession)
    [void](New-Item -ItemType Junction -Path $writerJunction -Value $EvidenceDirectory)
    Refuses { Open-SmokeOwnedProcessCueContext -ProjectRoot $root -Directory (Join-Path $writerJunction 'escape') -SessionId $aliasSession -ExpiresUtc ([DateTimeOffset]::UtcNow.AddMinutes(10).ToString('o')) -TimeoutSeconds 5 } 'alias/reparse'
    Check (!(Test-Path -LiteralPath (Join-Path $EvidenceDirectory 'escape'))) 'writer reparse rejection creates no escaped output'
    $link=Join-Path $EvidenceDirectory 'cue-hardlink.json'
    [void](New-Item -ItemType HardLink -Path $link -Value $cuePath)
    Refuses { Read-SmokeCueJson $link } 'alias/hardlink'
    Check (@($children | Where-Object { !$_.HasExited }).Count -eq 0) 'all tiny actors exited'
    [void](Complete-SmokeReadOnlyProfile -ProjectRoot $fixture.project -Profile $profile)
    $leaseClosed=$true
    $report=[ordered]@{passed=$true;checks=$checks.Count;names=@($checks);powershell=$PSVersionTable.PSVersion.ToString();
        observed_utc=[DateTime]::UtcNow.ToString('o');fixture_root=$fixture.project;cue_directories=@($contexts|ForEach-Object Directory);
        game_launches=0;owned_actors_remaining=0;lease_closed=$leaseClosed;deletion_performed=$false}
    [IO.File]::WriteAllText((Join-Path $EvidenceDirectory 'result.json'),($report|ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($false)))
    [pscustomobject]$report | Select-Object passed,checks,powershell,game_launches,owned_actors_remaining,lease_closed | ConvertTo-Json -Compress
} finally {
    foreach ($record in $ownerships) { [void](Stop-SmokeOwnedProcess $record) }
    foreach ($child in $children) {
        try {
            if (!$child.HasExited) {
                $child.StandardInput.Close()
                if (!$child.WaitForExit(5000)) { throw 'Tiny fixture still alive; lease retained' }
            }
        } finally { $child.Dispose() }
    }
    foreach ($context in $contexts) { $context.Guard.Dispose() }
    $consumerLock.Dispose(); $fixtureLock.Dispose()
    # On failure the tiny lease and all bytes remain for diagnosis.
}
# End of retained owned-process cue tests.
