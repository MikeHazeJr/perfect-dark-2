#Requires -Version 5.1
# Wrapper integration only: tiny byte fixtures, no native game/firewall/build.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../devtools/_build-env-prelude.ps1')
. (Join-Path $PSScriptRoot 'lib/Install-Harness.ps1')
$pathBeforeStorage = $env:PATH
$tempBeforeStorage = $env:TEMP
$tmpBeforeStorage = $env:TMP
$pythonExe = Get-SmokeStoragePython
$pythonPlatform = & $pythonExe -B -c "import os, sys; print(os.name + ':' + sys.platform)"
if ($LASTEXITCODE -ne 0 -or $pythonPlatform -ne 'nt:win32') { throw 'Storage selected a non-native Windows interpreter' }
$canonical = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$id = [Guid]::NewGuid().ToString('N')
$project = Join-Path $canonical ".claude/smoke-storage-selftests/ps-$id path with spaces"
$build = Join-Path $project 'Build'
$data = Join-Path $project '.claude/smoke-verify-cache/unit'
New-Item -ItemType Directory -Path $build,$data -Force | Out-Null
[IO.File]::WriteAllBytes((Join-Path $build 'PerfectDark.exe'), [byte[]](1,2,3,4))
[IO.File]::WriteAllBytes((Join-Path $build 'pd.unit.z64'), [byte[]](5,6,7,8))
[IO.File]::WriteAllBytes((Join-Path $build 'runtime.dll'), [byte[]](9,10,11,12))
[IO.File]::WriteAllText((Join-Path $data 'native.pdmesh'), 'editable source')
$definition = Join-Path $project 'test.json'
[IO.File]::WriteAllText($definition, '{"name":"wrapper-unit"}')
$script:SmokeStoragePolicy = @{schema=1;min_free_gib=0;max_full_installs=3;max_full_install_gib=0.001;
    max_archive_gib=0.004;peak_run_gib=0;extra_growth_gib=0;peak_build_gib=0}
$watch = [Diagnostics.Stopwatch]::StartNew()
$preflight = Assert-SmokeStoragePreflight -ProjectRoot $project -Build
if ($preflight.pending_bytes -ne 0) { throw 'Build-specific policy was ignored' }
$script:SmokeStoragePolicy.min_free_gib = 1073741824
$refused = $false
try { [void](Assert-SmokeStoragePreflight -ProjectRoot $project -Build) } catch { $refused = $true }
finally { $script:SmokeStoragePolicy.min_free_gib = 0 }
if (-not $refused) { throw 'Build free-space refusal was bypassed' }
$a = New-SmokeInstall -RunRoot (Join-Path $project 'unused') -ProjectRoot $project -TestName wrapper-unit -InstallState prefilled
Assert-SmokePlainTree -Path $a.InstallDir -Descendants
$rejected = $false
try { [void](Get-SmokeFixtureDestination -InstallDir $a.InstallDir -RelativePath '../../escape') } catch { $rejected = $true }
if (-not $rejected) { throw 'Fixture traversal was accepted' }
if (-not (Test-Path -LiteralPath (Join-Path $a.InstallDir 'runtime.dll'))) { throw 'DLL source list failed' }
if (-not (Test-Path -LiteralPath (Join-Path $a.InstallDir 'data/unit/native.pdmesh'))) { throw 'Prefilled source failed' }
$receipt = Complete-SmokeManagedInstall -ProjectRoot $project -InstallInfo $a -DefinitionPath $definition -Passed $true -Keep $false -Outcome @{name='wrapper-unit';passed=$true}
$b = New-SmokeSharedInstall -ProjectRoot $project -TestName shared-unit -InstallState prefilled
[IO.File]::WriteAllText((Join-Path $b.InstallDir 'data/unit/native.pdmesh'), 'changed')
[void](Complete-SmokeManagedInstall -ProjectRoot $project -InstallInfo $b -DefinitionPath $definition -Passed $true -Keep $false)
$c = New-SmokeSharedInstall -ProjectRoot $project -TestName shared-unit -InstallState prefilled
if ([IO.File]::ReadAllText((Join-Path $c.InstallDir 'data/unit/native.pdmesh')) -ne 'editable source') { throw 'Mutation leaked between tests' }
if ($c.StorageReusedBytes -ne 12) { throw 'Unexpected binary/ROM/DLL reuse' }
[void](Complete-SmokeManagedInstall -ProjectRoot $project -InstallInfo $c -DefinitionPath $definition -Passed $true -Keep $false)
$template = New-SmokeManagedInstall -ProjectRoot $project -TestName template-unit -TemplateInstall $a.InstallDir
if ($template.InstallDir -eq $a.InstallDir) { throw 'Existing input template was not isolated' }
[IO.File]::WriteAllText((Join-Path $template.InstallDir 'data/unit/native.pdmesh'), 'private template mutation')
if ([IO.File]::ReadAllText((Join-Path $a.InstallDir 'data/unit/native.pdmesh')) -ne 'editable source') { throw 'Input template changed' }
[void](Complete-SmokeManagedInstall -ProjectRoot $project -InstallInfo $template -DefinitionPath $definition -Passed $true -Keep $false)
$frozen = New-SmokeManagedInstall -ProjectRoot $project -TestName frozen-wrapper-unit -SourceSeed $a.StorageSeed
if ($frozen.StorageSeed -ne $a.StorageSeed) { throw 'Frozen seed identity changed' }
if ($frozen.RomId -ne 'unit' -or $frozen.SourceBinary -ne (Join-Path $frozen.InstallDir 'PerfectDark.exe')) {
    throw 'Frozen wrapper input binding failed'
}
if ([IO.File]::ReadAllText((Join-Path $frozen.InstallDir 'data/unit/native.pdmesh')) -ne 'editable source') {
    throw 'Frozen public source differs'
}
$ambiguous = $false
try { [void](New-SmokeManagedInstall -ProjectRoot $project -TestName ambiguous -SourceSeed $a.StorageSeed -SourceBinary (Join-Path $build 'PerfectDark.exe')) }
catch { $ambiguous = $true }
if (-not $ambiguous) { throw 'Frozen wrapper accepted a mutable binary override' }
[void](Complete-SmokeManagedInstall -ProjectRoot $project -InstallInfo $frozen -DefinitionPath $definition -Passed $true -Keep $false)
$inventory = Invoke-SmokeStorage -Action dry-run -ProjectRoot $project
if ($inventory.DeletionPerformed) { throw 'Dry run deleted content' }
if ($env:PATH -cne $pathBeforeStorage -or $env:TEMP -cne $tempBeforeStorage -or $env:TMP -cne $tmpBeforeStorage) {
    throw 'Storage interpreter selection changed the canonical build environment'
}
$watch.Stop()
$report = @{passed=$true;checks=17;seconds=$watch.Elapsed.TotalSeconds;shared_copied_bytes=$c.StorageCopiedBytes;
    shared_reused_bytes=$c.StorageReusedBytes;receipt=$receipt.Receipt;fixture_root=$project;game_launched=$false}
$report.python_executable = $pythonExe
$report.python_platform = $pythonPlatform
$report.path_python = (Get-Command python -CommandType Application).Source
$report.build_environment_unchanged = $true
$evidenceDir = Join-Path $canonical ".claude/smoke-storage-validation/ps-$id"
New-Item -ItemType Directory -Path $evidenceDir | Out-Null
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidenceDir 'results.json') -Encoding UTF8
# Leave these few KiB as inspectable wrapper evidence; no historical tree scan.
$report | ConvertTo-Json
Write-Output "Evidence: $evidenceDir/results.json"
