#Requires -Version 5.1
# Native wrapper proof with retained tiny fixtures. No game, full copy or deletion.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
. (Join-Path $PSScriptRoot 'lib/Reuse-Runtime.ps1')
$pythonExe = Get-SmokeStoragePython
$fixture = & $pythonExe -B (Join-Path $PSScriptRoot 'test-storage-reuse.py') --fixture | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Fixture setup failed' }
$script:SmokeStoragePolicy = @{schema=1;min_free_gib=0;max_full_installs=1;max_full_install_gib=65536/1GB;
    max_archive_gib=0.004;peak_run_gib=1024/1GB;extra_growth_gib=0;peak_build_gib=0}
$arguments = @{ProjectRoot=$fixture.project;InstallId=$fixture.id;Seed=$fixture.seed_id;BinarySha256=$fixture.binary_sha256}
$callerEncoding = $OutputEncoding
$storageRoot = Join-Path $fixture.project '.claude/smoke-storage'
$statePath = Join-Path $storageRoot 'state.json'
$priorRecipe = Join-Path $storageRoot 'receipts/completed-failure/recipe.json.gz'
$base = Join-Path $storageRoot 'shared/client'
$before = @{}
foreach ($file in @(Get-ChildItem -LiteralPath $base -Recurse -File) + @(Get-Item -LiteralPath $priorRecipe)) {
    $before[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
}
$stateHash = (Get-FileHash -LiteralPath $statePath).Hash
$plan = Get-SmokeReusePlan @arguments
if ($plan.RuntimeAdmitted -or $plan.PriorPassed -or $plan.StorageCopiedBytes -ne 0) { throw 'Plan claimed runtime or changed failure' }
if ((Get-FileHash -LiteralPath $statePath).Hash -ne $stateHash) { throw 'Plan changed registry' }

$profile = New-SmokeReadOnlyProfile @arguments
if ($profile.StorageCopiedBytes -ne 0 -or $profile.RuntimeAdmitted) { throw 'Storage profile copied/claimed a runtime' }
foreach ($path in $profile.ProfileDir,$profile.LogDir,$profile.CaptureDir) {
    if ($path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Profile writes overlap base' }
    if (@(Get-ChildItem -LiteralPath $path -Force).Count) { throw 'Profile directory is not fresh' }
}
[IO.File]::WriteAllText((Join-Path $profile.ProfileDir 'pd.ini'), 'new fixture profile')
[IO.File]::WriteAllText((Join-Path $profile.LogDir 'new.log'), 'new fixture evidence')
$refused = $false
try { [void](New-SmokeReadOnlyProfile @arguments) } catch { $refused = $true }
if (-not $refused) { throw 'Concurrent exclusive owner admitted' }
$closed = Complete-SmokeReadOnlyProfile -ProjectRoot $fixture.project -Profile $profile -Outcome @{consumer='read-only fixture';passed=$true}
if (-not $closed.BaseUnchanged -or $closed.DeletionPerformed -or $closed.PriorPassed) { throw 'Close changed original failure or deleted files' }
if (-not (Test-Path -LiteralPath (Join-Path $profile.LogDir 'new.log'))) { throw 'Close discarded profile evidence' }
foreach ($path in $before.Keys) {
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $before[$path]) { throw "Base/receipt changed: $path" }
}
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if (@($state.installs.PSObject.Properties).Count -ne 1 -or $state.installs.'completed-failure'.passed) { throw 'Full-copy cap or failure changed' }
$next = New-SmokeReadOnlyProfile @arguments
if ($next.ProfileDir -eq $profile.ProfileDir) { throw 'New run reused writable evidence' }
[void](Complete-SmokeReadOnlyProfile -ProjectRoot $fixture.project -Profile $next)
foreach ($wireCallerEncoding in @([Text.Encoding]::ASCII, (New-Object Text.UTF8Encoding($true)))) {
    try {
        $OutputEncoding = $wireCallerEncoding
        $encodedProfile = New-SmokeReadOnlyProfile @arguments
        [void](Complete-SmokeReadOnlyProfile -ProjectRoot $fixture.project -Profile $encodedProfile)
        if ($OutputEncoding -ne $wireCallerEncoding) { throw 'Storage changed caller output encoding' }
    } finally { $OutputEncoding = $callerEncoding }
}
foreach ($action in 'preflight','status','dry-run') {
    [void](Invoke-SmokeStorage -Action $action -ProjectRoot $fixture.project)
}

# Execute the production entry-point refusal, before build/native helpers/storage setup.
$refused = $false
try {
    & (Join-Path $PSScriptRoot 'run.ps1') -ReuseInstallId $fixture.id -ReuseSeed $fixture.seed_id -ReuseBinarySha256 $fixture.binary_sha256
} catch {
    if ($_.Exception.Message -notlike '*Runtime reuse refused*explicit verified native consumer*') { throw }
    $refused = $true
}
if (-not $refused) { throw 'Current mutating game runtime was admitted' }
$conflictRefused = $false
try {
    & (Join-Path $PSScriptRoot 'run.ps1') -ReuseInstallId $fixture.id -ReuseSeed $fixture.seed_id -ReuseBinarySha256 $fixture.binary_sha256 -Build
} catch {
    if ($_.Exception.Message -notlike '*conflicts*') { throw }
    $conflictRefused = $true
}
if (-not $conflictRefused) { throw 'Reuse invoked a build' }
$vetted=[pscustomobject]@{Name='menu_virtual_controller_agent_cancel';Path=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'tests/menu_virtual_controller_agent_cancel.json'));Definition=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tests/menu_virtual_controller_agent_cancel.json') -Raw | ConvertFrom-Json)}
Assert-SmokeReuseDefinition -Test $vetted
foreach ($property in @('fixtures','remove_paths','processes','packed_fixtures','fault_processes','target','runtime_strategy','custom_fixtures')) {
    $copy=$vetted | ConvertTo-Json -Depth 30 | ConvertFrom-Json
    $copy.Definition | Add-Member -NotePropertyName $property -NotePropertyValue @()
    $denied=$false
    try { Assert-SmokeReuseDefinition -Test $copy } catch { $denied=$true }
    if (!$denied) { throw "Reuse admitted declaration $property" }
}
$keyboard=[pscustomobject]@{Name='menu_settings_keyboard';Definition=(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'tests/menu_settings_keyboard.json') -Raw | ConvertFrom-Json)}
$denied=$false
try { Assert-SmokeReuseDefinition -Test $keyboard } catch { $denied=$true }
if (!$denied) { throw 'Mutating keyboard fixture admitted' }
$modified=$vetted | ConvertTo-Json -Depth 30 | ConvertFrom-Json
$modified.Definition.timeout_seconds=1
$denied=$false
try { Assert-SmokeReuseDefinition -Test $modified } catch { $denied=$true }
if (!$denied) { throw 'Changed canonical fixture caps admitted' }
$modified=$vetted | ConvertTo-Json -Depth 30 | ConvertFrom-Json
$modified.Path=Join-Path $fixture.project 'menu_virtual_controller_agent_cancel.json'
$denied=$false
try { Assert-SmokeReuseDefinition -Test $modified } catch { $denied=$true }
if (!$denied) { throw 'Unvetted alternate fixture path admitted' }
$tiny=Join-Path $fixture.project 'retained-consumer-identity.txt'
[IO.File]::WriteAllText($tiny,'synthetic identity; never executed')
$tinyHash=(Get-FileHash -LiteralPath $tiny -Algorithm SHA256).Hash
$lock=Open-SmokeReuseConsumer -Path $tiny -Sha256 $tinyHash
try {
    if ($lock.Hash() -ne $tinyHash.ToLowerInvariant()) { throw 'Locked consumer hash incorrect' }
    $denied=$false
    try { [IO.File]::WriteAllText($tiny,'must refuse') } catch { $denied=$true }
    if (!$denied) { throw 'Consumer byte lock permitted overwrite' }
} finally { $lock.Dispose() }
$denied=$false
try { $bad=Open-SmokeReuseConsumer -Path $tiny -Sha256 ('0'*64); $bad.Dispose() } catch { $denied=$true }
if (!$denied) { throw 'Consumer SHA mismatch admitted' }
$alias=Join-Path $fixture.project 'retained-consumer-hardlink.txt'
[void](New-Item -ItemType HardLink -Path $alias -Value $tiny)
$denied=$false
try { $bad=Open-SmokeReuseConsumer -Path $alias -Sha256 $tinyHash; $bad.Dispose() } catch { $denied=$true }
if (!$denied) { throw 'Consumer hardlink admitted' }
$report = @{passed=$true;storage_checks=19;adapter_fixture_admission=$true;consumer_lock_and_alias_refusals=$true;fixture_root=$fixture.project;python=$pythonExe;
    powershell=$PSVersionTable.PSVersion.ToString();game_launched=$false;full_game_copied=$false;
    deletion_performed=$false;prior_failure_preserved=$true;current_runtime_refused=$true;receipt=$closed.Receipt;
    utf8_transport_independent_of_caller=$true;caller_output_encoding_preserved=$true}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture.project 'wrapper-results.json') -Encoding UTF8
$report | ConvertTo-Json
