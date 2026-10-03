#Requires -Version 5.1
# Native wrapper proof with retained tiny fixtures. No game, full copy or deletion.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
$pythonExe = Get-SmokeStoragePython
$fixture = & $pythonExe -B (Join-Path $PSScriptRoot 'test-storage-reuse.py') --fixture | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Fixture setup failed' }
$script:SmokeStoragePolicy = @{schema=1;min_free_gib=0;max_full_installs=1;max_full_install_gib=65536/1GB;
    max_archive_gib=0.004;peak_run_gib=1024/1GB;extra_growth_gib=0;peak_build_gib=0}
$arguments = @{ProjectRoot=$fixture.project;InstallId=$fixture.id;Seed=$fixture.seed_id;BinarySha256=$fixture.binary_sha256}
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

# Execute the production entry-point refusal, before build/native helpers/storage setup.
$refused = $false
try {
    & (Join-Path $PSScriptRoot 'run.ps1') -ReuseInstallId $fixture.id -ReuseSeed $fixture.seed_id -ReuseBinarySha256 $fixture.binary_sha256
} catch {
    if ($_.Exception.Message -notlike '*Runtime reuse refused*extraction sidecars*.pdextract-cache*') { throw }
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
$report = @{passed=$true;checks=14;fixture_root=$fixture.project;python=$pythonExe;
    powershell=$PSVersionTable.PSVersion.ToString();game_launched=$false;full_game_copied=$false;
    deletion_performed=$false;prior_failure_preserved=$true;current_runtime_refused=$true;receipt=$closed.Receipt}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture.project 'wrapper-results.json') -Encoding UTF8
$report | ConvertTo-Json
