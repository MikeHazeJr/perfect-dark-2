#Requires -Version 7.0
<#
Milestone 2 of the build queue spec: the lease store and the CLI, against an
isolated queue root with a tiny dev repository. Builds and pushes nothing.

Checks (spec, Milestone 2 plus "Claude and Codex in parallel"):
  claim then renew keeps a lease; no renew for 10 minutes frees it; a fake boot
  time frees it; killing the owner frees it; a recycled PID with a different
  start time frees it; a submit with an older lease number is rejected; claim on
  a landed issue refuses without -Reopen; break frees and logs; a checkpoint
  survives a killed session and its lease expiry; the other agent gets the patch
  and cannot submit until it validates; agent codex off refuses a Codex claim
  and frees Codex leases; -Until turns the agent back on by itself; submit
  writes the job and patch, and the lease is the job's until it ends; and which
  process claim records for this session when it runs inside an agent host.
#>
param([switch] $KeepFixture)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\Pd2Queue.psm1') -Force
$git = Get-Pd2qGitExe
$pwsh = (Get-Process -Id $PID).Path
$cli = Join-Path $PSScriptRoot '..\pd2q.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('pd2q-leases-' + [guid]::NewGuid().ToString('N').Substring(0, 10))
$checks = New-Object 'Collections.Generic.List[string]'
function Check([bool] $Ok, [string] $Label) { if (-not $Ok) { throw "Check failed: $Label" }; $checks.Add($Label) }
function Q([string[]] $Arguments, [hashtable] $Env = @{}) {
    # One child pwsh per call, as sessions run it.
    $saved = @{}
    foreach ($k in $Env.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $Env[$k]) }
    try {
        $ErrorActionPreference = 'Continue'
        $out = & $pwsh -NoProfile -File $cli @Arguments 2>&1 | ForEach-Object { "$_" } | Out-String
        return [pscustomobject]@{ Code = $LASTEXITCODE; Out = $out }
    } finally {
        foreach ($k in $Env.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
    }
}
function Owner { Start-Process -FilePath $pwsh -ArgumentList @('-NoProfile', '-Command', 'Start-Sleep 600') -PassThru -WindowStyle Hidden }
function Leases { Get-Content -LiteralPath (Join-Path $root 'queue\leases.json') -Raw | ConvertFrom-Json -AsHashtable -DateKind String }
function LeaseNumber([string] $Text) { if ($Text -match 'lease (\d+) on') { return [long]$Matches[1] }; throw "no lease number in: $Text" }

$owners = @()
$env:PD2Q_ROOT = $root
# The test's items (T-UNIT-*) are not in the Workbench, so the repository's
# commit-msg check is off except where a check turns it on.
$env:PD2Q_CANONICAL = Join-Path $root 'no-main-checkout'
try {
    # A tiny dev repository standing in for the clean dev clone.
    $dev = Join-Path $root 'dev'
    [IO.Directory]::CreateDirectory((Join-Path $dev 'src')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $dev 'src\a.c'), "int a(void) { return 1; }`n")
    [IO.File]::WriteAllText((Join-Path $dev 'README.md'), "dev`n")
    [IO.Directory]::CreateDirectory((Join-Path $dev 'docs')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $dev 'docs\Dev Window.txt'), "a tracked name with a space, like devtools/Dev Window.bat`n")
    & $git -C $dev init -q -b dev; & $git -C $dev config user.name t; & $git -C $dev config user.email t@t; & $git -C $dev config core.autocrlf false
    & $git -C $dev add -A; & $git -C $dev commit -q -m base
    $start = [datetime]::UtcNow

    # Claim, renew, keep.
    $o1 = Owner; $owners += $o1
    $r = Q @('claim', 'T-UNIT-001', '-Session', 's1', '-OwnerPid', $o1.Id)
    Check ($r.Code -eq 0) "claim succeeds`n$($r.Out)"
    $n1 = LeaseNumber $r.Out
    $r = Q @('renew', 'T-UNIT-001', '-Lease', $n1)
    Check ($r.Code -eq 0 -and $r.Out -match 'renewed') 'renew keeps the lease'
    $r = Q @('claim', 'T-UNIT-001', '-Session', 's2', '-OwnerPid', $o1.Id)
    Check ($r.Code -ne 0 -and $r.Out -match 'held by s1|is held by s1') 'a held issue refuses another claim'
    $r = Q @('claim', 'T-UNIT-009', '-Session', 's1', '-OwnerPid', $o1.Id)
    Check ($r.Code -ne 0 -and $r.Out -match 's1 already holds lease \d+ on T-UNIT-001') "a session holding a lease cannot claim a second issue (one scratch folder)`n$($r.Out)"
    $r = Q @('claim', 'T-UNIT-009', '-Session', 'S1', '-OwnerPid', $o1.Id)
    Check ($r.Code -ne 0 -and $r.Out -match 'already holds lease \d+ on T-UNIT-001') "nor can the same name in another case (Windows: one folder)`n$($r.Out)"
    foreach ($badName in @('s1.', 'nul', 'COM1', 'con.txt')) {
        $r = Q @('claim', 'T-UNIT-009', '-Session', $badName, '-OwnerPid', $o1.Id)
        Check ($r.Code -ne 0 -and $r.Out -match 'may not end with a dot|is a Windows device name') "session '$badName' is refused (Windows would not keep it as its own folder)"
    }

    # No renew for 10 minutes frees it.
    $r = Q @('check', 'T-UNIT-001') @{ PD2Q_TEST_NOW_UTC = $start.AddMinutes(11).ToString('o') }
    Check ($r.Out -match 'T-UNIT-001 : free' -and -not (Leases).leases['T-UNIT-001']) "no renew for 10 minutes frees it`n$($r.Out)"

    # A fake boot time frees it.
    $r = Q @('claim', 'T-UNIT-002', '-Session', 's1', '-OwnerPid', $o1.Id)
    Check ($r.Code -eq 0) 'claim 2'
    $r = Q @('check', 'T-UNIT-002') @{ PD2Q_TEST_BOOT_UTC = '2030-01-01T00:00:00Z' }
    Check (-not (Leases).leases['T-UNIT-002']) 'a different boot time frees it'
    Check ((Get-Content -LiteralPath (Join-Path $root 'queue\service.log') -Raw) -match 'T-UNIT-002 freed: the PC rebooted') 'the reason is logged'

    # Killing the owner frees it.
    $o2 = Owner; $owners += $o2
    $r = Q @('claim', 'T-UNIT-003', '-Session', 's3', '-OwnerPid', $o2.Id)
    Check ($r.Code -eq 0) 'claim 3'
    Stop-Process -Id $o2.Id -Force; $o2.WaitForExit()
    $r = Q @('check', 'T-UNIT-003')
    Check (-not (Leases).leases['T-UNIT-003']) 'killing the owner frees it'

    # A recycled PID: same id, different start time.
    $r = Q @('claim', 'T-UNIT-004', '-Session', 's1', '-OwnerPid', $o1.Id)
    $l = Leases; $l.leases['T-UNIT-004'].holder.pidStartUtc = '2020-01-01T00:00:00.000Z'
    [IO.File]::WriteAllText((Join-Path $root 'queue\leases.json'), ($l | ConvertTo-Json -Depth 10))
    $r = Q @('check', 'T-UNIT-004')
    Check (-not (Leases).leases['T-UNIT-004']) 'a recycled PID with a different start time frees it'

    # Older lease number rejected at submit.
    $r = Q @('claim', 'T-UNIT-005', '-Session', 's1', '-OwnerPid', $o1.Id); $old = LeaseNumber $r.Out
    $r = Q @('break', 'T-UNIT-005', '-Reason', 'unit test break', '-Session', 'tester')
    Check ($r.Code -eq 0 -and (Get-Content -LiteralPath (Join-Path $root 'queue\service.log') -Raw) -match 'break T-UNIT-005 by tester \(unit test break\)') 'break frees and logs who and why'
    $r = Q @('claim', 'T-UNIT-005', '-Session', 's1', '-OwnerPid', $o1.Id); $new = LeaseNumber $r.Out
    $r = Q @('edit', 'src/a.c', '-Session', 's1')
    $edited = $r.Out.Trim()
    [IO.File]::WriteAllText($edited, "int a(void) { return 2; }`n")
    $r = Q @('submit', 'T-UNIT-005', '-Lease', $old, '-Title', 'Tooling - T-UNIT-005: Return two')
    Check ($r.Code -ne 0 -and $r.Out -match "Lease $old on T-UNIT-005 is not current; the current lease is $new") 'an older lease number is rejected with both numbers'

    # Submit: job, patch, lease belongs to the job, scratch cleared.
    $r = Q @('submit', 'T-UNIT-005', '-Lease', $new, '-Title', 'not the standard title')
    Check ($r.Code -ne 0 -and $r.Out -match 'Title must be') 'a title outside the commit standard is refused'
    $bodyFile = Join-Path $root 'body.txt'
    [IO.File]::WriteAllText($bodyFile, "Return two.`n`nPD2-Issue: T-OTHER-001`n")
    $r = Q @('submit', 'T-UNIT-005', '-Lease', $new, '-Title', 'Tooling - T-UNIT-005: Return two', '-Body', $bodyFile)
    Check ($r.Code -ne 0 -and $r.Out -match 'may not carry trailer lines') 'a body with its own trailer line is refused (the board reads PD2-Issue from git)'
    $r = Q @('submit', 'T-UNIT-005', '-Lease', $new, '-Title', 'Tooling - T-UNIT-005: Return two', '-CoAuthor', 'no email here')
    Check ($r.Code -ne 0 -and $r.Out -match "must look like 'Name <email>'") 'a co-author without an email is refused'
    [IO.File]::WriteAllText($bodyFile, "Return two, so the unit test has a patch.`n")
    $r = Q @('submit', 'T-UNIT-005', '-Lease', $new, '-Title', 'Tooling - T-UNIT-005: Return two', '-Body', $bodyFile, '-CoAuthor', 'Unit Test <noreply@example.invalid>')
    Check ($r.Code -eq 0 -and $r.Out -match '(q\d+-\d{8}) queued') "submit queues a job`n$($r.Out)"
    $job = ([regex]::Match($r.Out, 'q\d+-\d{8}')).Value
    $patchText = Get-Content -LiteralPath (Join-Path $root "queue\journal\$job.patch") -Raw
    Check ($patchText -match '\+int a\(void\) \{ return 2; \}' -and $patchText -match 'diff --git a/src/a.c b/src/a.c') 'the patch is the scratch diff with repository paths'
    $request = (Get-Content -LiteralPath (Join-Path $root "queue\journal\$job.jsonl") | Select-Object -First 1) | ConvertFrom-Json
    Check ($request.body -eq 'Return two, so the unit test has a patch.' -and $request.coAuthor -eq 'Unit Test <noreply@example.invalid>') 'the request keeps the body and the co-author for the commit'
    Check ((Leases).leases['T-UNIT-005'].state -eq 'queued' -and (Leases).leases['T-UNIT-005'].job -eq $job) 'the lease belongs to the job'
    Check (-not (Test-Path -LiteralPath (Join-Path $root 'scratch\s1'))) 'submit clears scratch'
    $r = Q @('check', 'T-UNIT-005') @{ PD2Q_TEST_NOW_UTC = $start.AddMinutes(30).ToString('o') }
    Check ((Leases).leases['T-UNIT-005'] -and $r.Out -match 'needs testing') 'a queued lease outlives the session clock'
    $r = Q @('cancel', $job, '-Reason', 'unit test')
    Check ($r.Code -eq 0 -and -not (Leases).leases['T-UNIT-005']) 'cancelling a queued job frees its issue'
    $r = Q @('check', 'T-UNIT-005')
    Check ($r.Out -match 'started \(unowned\)' -and $r.Out -match 'saved work: 1 file') "a job cancelled before it ran gives its patch back to the task`n$($r.Out)"
    $r = Q @('claim', 'T-UNIT-005', '-Session', 's1', '-OwnerPid', $o1.Id)
    Check ($r.Code -eq 0 -and $r.Out -match 'Restored your saved work' -and (Get-Content -LiteralPath (Join-Path $root 'scratch\s1\src\a.c') -Raw) -match 'return 2') "the next claim restores the submitted edits`n$($r.Out)"
    [void](Q @('release', 'T-UNIT-005', '-Lease', (LeaseNumber $r.Out)))

    # Breaking a queued lease leaves its session's scratch alone: the session may
    # be editing its next issue there, and the queued patch was saved at submit.
    $r = Q @('claim', 'T-UNIT-012', '-Session', 's1', '-OwnerPid', $o1.Id); $n12 = LeaseNumber $r.Out
    $f12 = (Q @('edit', 'src/a.c', '-Session', 's1')).Out.Trim()
    [IO.File]::WriteAllText($f12, "int a(void) { return 12; }`n")
    $r = Q @('submit', 'T-UNIT-012', '-Lease', $n12, '-Title', 'Tooling - T-UNIT-012: Return twelve')
    Check ($r.Code -eq 0) "submit T-UNIT-012`n$($r.Out)"
    $r = Q @('claim', 'T-UNIT-013', '-Session', 's1', '-OwnerPid', $o1.Id); $n13 = LeaseNumber $r.Out
    Check ($r.Code -eq 0) 'a session whose lease is queued may claim its next issue'
    $f13 = (Q @('edit', 'src/a.c', '-Session', 's1')).Out.Trim()
    [IO.File]::WriteAllText($f13, "int a(void) { return 13; }`n")
    $r = Q @('checkpoint', 'T-UNIT-012', '-Lease', $n12, '-Note', 'would file the next issue under this one')
    Check ($r.Code -ne 0 -and $r.Out -match 'is queued' -and (Get-Content -LiteralPath (Join-Path $root 'queue\tasks\T-UNIT-012.patch') -Raw) -match 'return 12') "a checkpoint on a queued lease is refused, so its saved patch stays its own`n$($r.Out)"
    $r = Q @('break', 'T-UNIT-012', '-Reason', 'unit test: break a queued lease', '-Session', 'tester')
    Check ($r.Code -eq 0 -and (Test-Path -LiteralPath $f13) -and (Get-Content -LiteralPath $f13 -Raw) -match 'return 13') "breaking a queued lease leaves the session's next issue's scratch alone`n$($r.Out)"
    Check ((Get-Content -LiteralPath (Join-Path $root 'queue\tasks\T-UNIT-012.patch') -Raw) -match 'return 12') "and the queued issue's saved patch is still its own"
    Check ((Q @('check', 'T-UNIT-012')).Out -match 'started \(unowned\)') 'and the dropped job leaves its issue resumable'
    [void](Q @('release', 'T-UNIT-013', '-Lease', $n13))

    # Landed issue refuses a claim without -Reopen.
    [IO.File]::AppendAllText((Join-Path $root 'queue\board.jsonl'), ((@{ schema = 'pd2q.board.v1'; issue = 'B-9999'; commit = 'abcdef0123456789'; job = 'q1-20261122'; title = 't'; session = 's'; landedUtc = '2026-11-22T00:00:00.000Z' } | ConvertTo-Json -Compress) + "`n"))
    $r = Q @('claim', 'B-9999', '-Session', 's1', '-OwnerPid', $o1.Id)
    Check ($r.Code -ne 0 -and $r.Out -match 'already landed') 'claim on a landed issue refuses'
    $r = Q @('claim', 'B-9999', '-Session', 's1', '-OwnerPid', $o1.Id, '-Reopen', 'regressed in unit test')
    Check ($r.Code -eq 0) 'claim with -Reopen works'
    [void](Q @('release', 'B-9999', '-Lease', (LeaseNumber $r.Out)))

    # A checkpoint survives a killed session; the other agent gets the patch
    # and cannot submit until it validates.
    $o3 = Owner; $owners += $o3
    $r = Q @('claim', 'T-UNIT-006', '-Session', 'claude-a', '-OwnerPid', $o3.Id)
    $na = LeaseNumber $r.Out
    $file = (Q @('edit', 'src/a.c', '-Session', 'claude-a')).Out.Trim()
    [IO.File]::WriteAllText($file, "int a(void) { return 6; }`n")
    [void](Q @('edit', 'docs/Dev Window.txt', '-Session', 'claude-a', '-Delete'))
    $r = Q @('checkpoint', 'T-UNIT-006', '-Lease', $na, '-Note', 'changed a to 6, tests next')
    Check ($r.Code -eq 0) 'checkpoint'
    Stop-Process -Id $o3.Id -Force; $o3.WaitForExit()
    $r = Q @('check', 'T-UNIT-006')
    Check ($r.Out -match 'started \(unowned\)' -and $r.Out -match 'changed a to 6') "a killed session's checkpoint survives`n$($r.Out)"
    Check ((Get-Content -LiteralPath (Join-Path $root 'queue\tasks\T-UNIT-006.patch') -Raw) -match 'return 6') 'the saved patch holds the edit'
    $o4 = Owner; $owners += $o4
    $r = Q @('claim', 'T-UNIT-006', '-Session', 'codex-b', '-OwnerPid', $o4.Id, '-Agent', 'codex')
    Check ($r.Code -ne 0 -and $r.Out -match 'switched off') 'codex is off by default (Mike switched it off 2026-11-20)'
    [void](Q @('agent', 'codex', 'on', '-Reason', 'unit test'))
    $r = Q @('claim', 'T-UNIT-006', '-Session', 'codex-b', '-OwnerPid', $o4.Id, '-Agent', 'codex')
    Check ($r.Code -eq 0 -and $r.Out -match 'Restored the saved work') 'the other agent claims and gets the saved patch'
    $nb = LeaseNumber $r.Out
    Check ((Get-Content -LiteralPath (Join-Path $root 'scratch\codex-b\src\a.c') -Raw) -match 'return 6') 'the restored scratch holds the edit'
    Check (@(Get-Content -LiteralPath (Join-Path $root 'scratch\codex-b\.pd2q-deleted') -ErrorAction SilentlyContinue) -contains 'docs/Dev Window.txt') 'and the deletion of a name with a space (the next patch keeps it)'
    $r = Q @('submit', 'T-UNIT-006', '-Lease', $nb, '-Title', 'Tooling - T-UNIT-006: Return six')
    Check ($r.Code -ne 0 -and $r.Out -match 'not validated') 'an unvalidated task cannot be submitted'
    $r = Q @('validate', 'T-UNIT-006', '-Lease', $nb, '-Result', 'keep', '-Note', 'read the note and the diff; keep')
    Check ($r.Code -eq 0) 'validate keep'
    $r = Q @('submit', 'T-UNIT-006', '-Lease', $nb, '-Title', 'Tooling - T-UNIT-006: Return six')
    Check ($r.Code -eq 0) 'after validation it submits'
    $job6 = ([regex]::Match($r.Out, 'q\d+-\d{8}')).Value
    Check ((Get-Content -LiteralPath (Join-Path $root "queue\journal\$job6.jsonl") -Raw) -match '"agent":"codex"') 'the job records the agent'
    $patch6 = Get-Content -LiteralPath (Join-Path $root "queue\journal\$job6.patch") -Raw
    Check ($patch6 -match 'deleted file mode' -and $patch6 -match 'Dev Window\.txt' -and $patch6 -match 'return 6') "the submitted patch still deletes the spaced name after the handoff"
    [void](Q @('cancel', $job6, '-Reason', 'unit test'))

    # Agent codex off refuses a Codex claim and frees Codex leases; -Until.
    $r = Q @('claim', 'T-UNIT-007', '-Session', 'codex-b', '-OwnerPid', $o4.Id, '-Agent', 'codex')
    Check ($r.Code -eq 0) 'codex claim while on'
    $until = $start.AddMinutes(5).ToString('o')
    [void](Q @('agent', 'codex', 'off', '-Until', $until, '-Reason', 'weekly limit'))
    Check (-not (Leases).leases['T-UNIT-007']) 'switching codex off frees its leases'
    $r = Q @('claim', 'T-UNIT-008', '-Session', 'codex-b', '-OwnerPid', $o4.Id, '-Agent', 'codex')
    Check ($r.Code -ne 0 -and $r.Out -match 'switched off') 'a codex claim is refused while off'
    $r = Q @('claim', 'T-UNIT-008', '-Session', 'codex-b', '-OwnerPid', $o4.Id, '-Agent', 'codex') @{ PD2Q_TEST_NOW_UTC = $start.AddMinutes(6).ToString('o') }
    Check ($r.Code -eq 0) 'the -Until time turns codex back on by itself'

    # Reverting every edit clears the saved patch, so the next holder does not
    # get back work this session undid.
    $o5 = Owner; $owners += $o5
    $r = Q @('claim', 'T-UNIT-010', '-Session', 'claude-r', '-OwnerPid', $o5.Id); $nr = LeaseNumber $r.Out
    $fileR = (Q @('edit', 'src/a.c', '-Session', 'claude-r')).Out.Trim()
    [IO.File]::WriteAllText($fileR, "int a(void) { return 10; }`n")
    [void](Q @('checkpoint', 'T-UNIT-010', '-Lease', $nr, '-Note', 'a returns 10'))
    $patchR = Join-Path $root 'queue\tasks\T-UNIT-010.patch'
    Check (Test-Path -LiteralPath $patchR -PathType Leaf) 'a checkpoint saves the patch'
    [IO.File]::WriteAllText($fileR, "int a(void) { return 1; }`n")
    [void](Q @('renew', 'T-UNIT-010', '-Lease', $nr))
    $taskR = Get-Content -LiteralPath (Join-Path $root 'queue\tasks\T-UNIT-010.json') -Raw | ConvertFrom-Json -AsHashtable
    Check (-not (Test-Path -LiteralPath $patchR) -and -not $taskR.ContainsKey('patch') -and @($taskR.files).Count -eq 0) 'reverting every edit clears the saved patch at the next save'

    # A checkpoint that fails as a lease frees: the scratch, then the only copy of
    # the newest edits, moves to scratch\_kept and an alarm names it, so the
    # session's next claim (which restores the older saved patch) cannot clear it.
    [IO.File]::WriteAllText($fileR, "int a(void) { return 11; }`n")
    [void](Q @('checkpoint', 'T-UNIT-010', '-Lease', $nr, '-Note', 'a returns 11'))
    [IO.File]::WriteAllText($fileR, "int a(void) { return 12; }`n")  # newer, not saved
    (Get-Item -LiteralPath $patchR).IsReadOnly = $true  # saving the patch now fails
    Stop-Process -Id $o5.Id -Force; $o5.WaitForExit()
    [void](Q @('check', 'T-UNIT-010'))
    Check (-not (Leases).leases['T-UNIT-010']) "the dead owner's lease is freed"
    $kept = @(Get-ChildItem -LiteralPath (Join-Path $root 'scratch\_kept') -Directory -Filter 'claude-r-*' -ErrorAction SilentlyContinue)
    Check ($kept.Count -eq 1 -and (Get-Content -LiteralPath (Join-Path $kept[0].FullName 'src\a.c') -Raw) -match 'return 12' -and -not (Test-Path -LiteralPath (Join-Path $root 'scratch\claude-r'))) 'a failed checkpoint moves the scratch to scratch\_kept'
    # Parsed, not matched as text: a hashtable's keys have no fixed order in the JSON.
    $alarmR = @((Get-Content -LiteralPath (Join-Path $root 'queue\alarms.json') -Raw | ConvertFrom-Json).alarms | Where-Object { $_.kind -eq 'checkpoint-failed' -and $_.detail -match '_kept' })
    Check ($alarmR.Count -ge 1) 'and an alarm says where the edits are'
    $o7 = Owner; $owners += $o7
    $r = Q @('claim', 'T-UNIT-010', '-Session', 'claude-r', '-OwnerPid', $o7.Id)
    Check ($r.Code -eq 0 -and (Get-Content -LiteralPath $fileR -Raw) -match 'return 11' -and (Get-Content -LiteralPath (Join-Path $kept[0].FullName 'src\a.c') -Raw) -match 'return 12') "the same session's reclaim restores the saved patch and leaves the kept edits alone`n$($r.Out)"
    (Get-Item -LiteralPath $patchR).IsReadOnly = $false
    [void](Q @('release', 'T-UNIT-010', '-Lease', (LeaseNumber $r.Out)))

    # The repository's commit-msg check runs at submit where the main
    # checkout's Workbench is (not on CI): an item it does not know is refused.
    $real = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike'
    if ((Test-Path -LiteralPath (Join-Path $real 'Tools\Workbench\data\roadmap.json')) -and (Get-Pd2qPythonExe)) {
        $o6 = Owner; $owners += $o6
        $r = Q @('claim', 'T-UNIT-011', '-Session', 'claude-m', '-OwnerPid', $o6.Id); $nm = LeaseNumber $r.Out
        $fileM = (Q @('edit', 'src/a.c', '-Session', 'claude-m')).Out.Trim()
        [IO.File]::WriteAllText($fileM, "int a(void) { return 12; }`n")
        $r = Q @('submit', 'T-UNIT-011', '-Lease', $nm, '-Title', 'Tooling - T-UNIT-011: Return twelve') @{ PD2Q_CANONICAL = $real }
        Check ($r.Code -ne 0 -and $r.Out -match "commit-msg check" -and $r.Out -match 'does not exist in the Workbench roadmap') "submit runs the repository's commit-msg check (an unknown item is refused)`n$($r.Out)"
        [void](Q @('release', 'T-UNIT-011', '-Lease', $nm))
    } else { $checks.Add("commit-msg check at submit skipped: no main checkout or Python here") }

    # Editing a path again undoes its earlier -Delete, so the patch keeps the
    # file's new contents instead of deleting it.
    $o9 = Owner; $owners += $o9
    $r = Q @('claim', 'T-UNIT-015', '-Session', 'claude-u', '-OwnerPid', $o9.Id); $nu = LeaseNumber $r.Out
    [void](Q @('edit', 'README.md', '-Session', 'claude-u', '-Delete'))
    $fileU = (Q @('edit', 'README.md', '-Session', 'claude-u')).Out.Trim()
    [IO.File]::WriteAllText($fileU, "dev, edited after all`n")
    [void](Q @('checkpoint', 'T-UNIT-015', '-Lease', $nu, '-Note', 'deleted, then edited'))
    $patchU = Get-Content -LiteralPath (Join-Path $root 'queue\tasks\T-UNIT-015.patch') -Raw
    Check ($patchU -match '\+dev, edited after all' -and $patchU -notmatch 'deleted file mode') "a file deleted and then edited again is edited, not deleted`n$patchU"
    [void](Q @('release', 'T-UNIT-015', '-Lease', $nu))

    # B-1307: an empty byte array used to reach its caller as $null (PowerShell
    # unrolls it), so edit -New failed for every new file, and a file that is
    # empty in dev read as missing: it would not open without -New, and the
    # patch added it as a new file, which cannot apply to dev.
    [IO.File]::WriteAllBytes((Join-Path $dev 'docs\empty.txt'), [byte[]]::new(0))
    & $git -C $dev add -A; & $git -C $dev commit -q -m 'an empty file'
    $o10 = Owner; $owners += $o10
    $r = Q @('claim', 'T-UNIT-016', '-Session', 'claude-n', '-OwnerPid', $o10.Id); $nn = LeaseNumber $r.Out
    $r = Q @('edit', 'docs/new.txt', '-Session', 'claude-n', '-New')
    $fileN = $r.Out.Trim()
    Check ($r.Code -eq 0 -and (Test-Path -LiteralPath $fileN -PathType Leaf) -and (Get-Item -LiteralPath $fileN).Length -eq 0) "edit -New starts a new file empty in scratch`n$($r.Out)"
    [IO.File]::WriteAllText($fileN, "a new file`n")
    $r = Q @('edit', 'docs/empty.txt', '-Session', 'claude-n')
    $fileE = $r.Out.Trim()
    Check ($r.Code -eq 0 -and (Test-Path -LiteralPath $fileE -PathType Leaf) -and (Get-Item -LiteralPath $fileE).Length -eq 0) "a file that is empty in dev opens without -New`n$($r.Out)"
    [IO.File]::WriteAllText($fileE, "no longer empty`n")
    [void](Q @('checkpoint', 'T-UNIT-016', '-Lease', $nn, '-Note', 'a new file and an emptied one'))
    $patchN = Get-Content -LiteralPath (Join-Path $root 'queue\tasks\T-UNIT-016.patch') -Raw
    $sections = @($patchN -split '(?m)^diff --git ')
    $newSection = @($sections | Where-Object { $_.StartsWith('a/docs/new.txt ') })[0]
    $emptySection = @($sections | Where-Object { $_.StartsWith('a/docs/empty.txt ') })[0]
    Check ($newSection -and $newSection -match 'new file mode' -and $newSection -match '\+a new file') "the patch adds the new file`n$patchN"
    Check ($emptySection -and $emptySection -notmatch 'new file mode' -and $emptySection -match '\+no longer empty') "and edits the empty file rather than adding it`n$patchN"
    [void](Q @('release', 'T-UNIT-016', '-Lease', $nn))

    # submit -Patch declares both sides of a rename, names with spaces included,
    # so the worker's touched-files check accepts the rename's destination.
    [void](& $git -C $dev mv README.md 'docs/Read Me.md')
    $renamePatch = Join-Path $root 'rename.patch'
    [IO.File]::WriteAllBytes($renamePatch, [Text.Encoding]::UTF8.GetBytes(((& $git -C $dev diff --cached -M --binary) -join "`n") + "`n"))
    [void](& $git -C $dev reset -q --hard)
    $o8 = Owner; $owners += $o8
    $r = Q @('claim', 'T-UNIT-014', '-Session', 'claude-p', '-OwnerPid', $o8.Id); $np = LeaseNumber $r.Out
    $r = Q @('submit', 'T-UNIT-014', '-Lease', $np, '-Title', 'Tooling - T-UNIT-014: Move the readme', '-Patch', $renamePatch)
    Check ($r.Code -eq 0) "a rename patch submits`n$($r.Out)"
    $job14 = ([regex]::Match($r.Out, 'q\d+-\d{8}')).Value
    $req14 = (Get-Content -LiteralPath (Join-Path $root "queue\journal\$job14.jsonl") | Select-Object -First 1) | ConvertFrom-Json
    Check (@($req14.files) -contains 'README.md' -and @($req14.files) -contains 'docs/Read Me.md') "the request names both sides of the rename: $(@($req14.files) -join ' | ')"
    [void](Q @('cancel', $job14, '-Reason', 'unit test'))

    # The worker's -Root wins over PD2Q_ROOT (set for this whole test), so an
    # installed worker serves the root it was installed for; the installer passes it.
    $rootX = Join-Path $root 'other-root'
    $out = & $pwsh -NoProfile -File (Join-Path $PSScriptRoot '..\Queue-Service.ps1') -Setup -Root $rootX -Remote (Join-Path $root 'no-such-remote.git') 2>&1 | Out-String
    Check ((Test-Path -LiteralPath (Join-Path $rootX 'queue\service.log')) -and (Get-Content -LiteralPath (Join-Path $rootX 'queue\service.log') -Raw) -match 'setup: cloning dev') "Queue-Service.ps1 -Root lays out and logs under that root`n$out"
    Check ((Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Install-QueueService.ps1') -Raw) -match '-Foreground -Root `"\$Root`"') 'Install-QueueService.ps1 passes its -Root to the scheduled worker'

    # The Workbench gate before the worker writes landing notes (AGENTS.md):
    # canonical, not isolated, and both roots in the main checkout.
    $main = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike'
    $good = [pscustomobject]@{ canonical = $true; isolated = $false; projectRoot = $main; dataDir = "$main\Tools\Workbench\data" }
    Check (Test-Pd2qWorkbenchMeta $good $main) 'the main checkout Workbench passes the gate'
    # B-1308: a landing is noted on the title's Workbench item. A bug id issue is
    # not one, and the Workbench refused the note with 400.
    Check ((Get-Pd2qLandingNoteTarget @{ title = 'Tooling - T-TOOLING-020: Let pd2q edit create new files'; issue = 'B-1307' }) -ceq 'T-TOOLING-020') "a bug id job's landing is noted on its title's item"
    Check ((Get-Pd2qLandingNoteTarget @{ title = 'Release - T-RELEASE-006: Keep release.ps1 -DryRun from pushing'; issue = 'T-RELEASE-006' }) -ceq 'T-RELEASE-006') "and an item's own job on that item"
    Check (Test-Pd2qWorkbenchMeta ([pscustomobject]@{ canonical = $true; isolated = $false; projectRoot = 'c:/users/mikeh/perfect-dark-2/perfect_dark-mike/'; dataDir = 'C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike/Tools/Workbench/data' }) $main) 'whatever the case and slashes'
    foreach ($bad in @(
        @{ canonical = $true; isolated = $false; projectRoot = 'D:\other-clone'; dataDir = 'D:\other-clone\Tools\Workbench\data' },
        @{ canonical = $true; isolated = $false; projectRoot = $main; dataDir = 'D:\elsewhere\data' },
        @{ canonical = $true; isolated = $true; projectRoot = $main; dataDir = "$main\Tools\Workbench\data" },
        @{ canonical = $false; isolated = $false; projectRoot = $main; dataDir = "$main\Tools\Workbench\data" })) {
        Check (-not (Test-Pd2qWorkbenchMeta ([pscustomobject]$bad) $main)) "a Workbench that is not the main checkout's is refused ($($bad.projectRoot), $($bad.dataDir), isolated=$($bad.isolated), canonical=$($bad.canonical))"
    }

    # Milestone 4: the push target is dev and nothing else.
    Import-Module (Join-Path $PSScriptRoot '..\Pd2Queue.psm1') -Force
    $msg = Get-Pd2qCommitMessage @{ title = 'Networking - T-NETWORKING-007: Fix rejoin'; issue = 'B-1277'; session = 's'; agent = 'claude'; body = 'Why.'; coAuthor = 'A <a@b.c>' } 'q9-20261122' 'Built.'
    Check ($msg -match '(?m)^Refs: T-NETWORKING-007, B-1277$' -and $msg -match '(?m)^PD2-Issue: B-1277$' -and ($msg.TrimEnd() -split "`n")[-1] -eq 'Co-Authored-By: A <a@b.c>') "Refs names the title's item and the bug id`n$msg"
    Assert-Pd2qPushTarget 'refs/heads/dev'
    foreach ($bad in @('refs/heads/main', 'refs/heads/Dev', 'dev', 'refs/heads/dev2', 'refs/tags/dev', 'HEAD:refs/heads/main')) {
        $refused = $false
        try { Assert-Pd2qPushTarget $bad } catch { $refused = $_.Exception.Message -match 'pushes only refs/heads/dev' }
        Check $refused "a push to '$bad' is refused"
    }

    # Which process claim records for a real session (only inside an agent host).
    try {
        $me = Resolve-Pd2qOwner
        Check ($me.process -in @('claude.exe', 'codex.exe', 'node.exe') -and $me.pid -ne $PID) ("claim records the agent host " + $me.process + ' pid ' + $me.pid)
    } catch {
        $checks.Add('agent host check skipped: not running under an agent host (' + $_.Exception.Message + ')')
    }
} finally {
    foreach ($o in $owners) { try { if (-not $o.HasExited) { Stop-Process -Id $o.Id -Force } } catch { } }
    Remove-Item Env:PD2Q_ROOT, Env:PD2Q_CANONICAL -ErrorAction SilentlyContinue
    if (-not $KeepFixture -and (Test-Path -LiteralPath $root)) {
        Get-ChildItem -LiteralPath $root -Recurse -Force -File | ForEach-Object { $_.Attributes = 'Normal' }
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
$checks | ForEach-Object { "  ok  $_" }
"PASS: $($checks.Count) lease and CLI checks."
exit 0
