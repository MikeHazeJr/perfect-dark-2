#Requires -Version 5.1
# Synthetic tiny fixtures only. Does not launch/probe the game or a desktop.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'lib\Storage-Harness.ps1')
. (Join-Path $PSScriptRoot 'lib\Native-Consumer.ps1')
$script:SmokeStoragePython = 'C:/Python312/python.exe'
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('pd-native-helper-' + [guid]::NewGuid().ToString('N'))
$expectedRoot = [IO.Path]::GetFullPath($temporaryRoot)
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
$cases = 0
function Assert-Case([bool] $Value, [string] $Name) {
    if (-not $Value) { throw "FAIL: $Name" }; $script:cases++
}
function Reject-Copy([string] $Text, [string] $Name) {
    $path = Join-Path $temporaryRoot ($Name + '.ini')
    [IO.File]::WriteAllText($path, $Text, (New-Object Text.UTF8Encoding($false)))
    $install = Join-Path $temporaryRoot $Name; New-Item -ItemType Directory -Path $install | Out-Null
    $rejected = $false
    try { [void](Copy-SmokeNativeConsumerManifest -InstallDir $install -ManifestPath $path -BinaryPath $binary) }
    catch { $rejected = $true }
    Assert-Case $rejected $Name
    Assert-Case (-not (Test-Path (Join-Path $install 'native-asset-consumer.ini'))) ($Name + ' leaves no private config')
}
try {
    $binary = Join-Path $temporaryRoot 'fake-client.txt'; [IO.File]::WriteAllText($binary, 'Synthetic unit-test bytes, never executable.')
    $binaryHash = (Get-FileHash $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    $planner = Join-Path (Split-Path -Parent $PSScriptRoot) 'asset-inspection-plan.py'
    $fixtureCode = @'
import importlib.util,gzip,hashlib,json,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('seed_fixture',sys.argv[1]);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
files={'data/model.pdmesh':dict(sha256='a'*64,bytes=1,mtime_ns=1),'data/texture.pdtexture':dict(sha256='e'*64,bytes=1,mtime_ns=1),'PerfectDark.exe':dict(sha256=sys.argv[3],bytes=1,mtime_ns=1)}
raw=m.canonical(files);seed=hashlib.sha256(raw).hexdigest()
root=Path(sys.argv[2])/'.claude/smoke-storage/seeds';root.mkdir(parents=True)
(root/(seed+'.json.gz')).write_bytes(gzip.compress(raw))
print(json.dumps(dict(seed=seed,source=m.source_set_hash(files))))
'@
    $seedMetadata = & $script:SmokeStoragePython -B -c $fixtureCode $planner $temporaryRoot $binaryHash | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic seed fixture setup failed.' }
    $manifest = @"
schema=pd2.native-asset-consumer-manifest.v1
binary_sha256=$binaryHash
source_sha256=$($seedMetadata.source)
archive_sha256=$('a' * 64)
catalog_id=base:model
archive_origin=data/model.pdmesh
run_id=synthetic-helper
max_output_bytes=16777216
"@ + "`n"
    $path = Join-Path $temporaryRoot 'source.ini'; [IO.File]::WriteAllText($path, $manifest, (New-Object Text.UTF8Encoding($false)))
    $sourceHash = (Get-FileHash $path).Hash
    $install = Join-Path $temporaryRoot 'private'; New-Item -ItemType Directory -Path $install | Out-Null
    $info = Copy-SmokeNativeConsumerManifest -InstallDir $install -ManifestPath $path -BinaryPath $binary
    Assert-Case ($info.Bytes -lt 8192 -and $info.Values.catalog_id -eq 'base:model') 'exact private manifest'
    Assert-Case ((Get-FileHash $path).Hash -eq $sourceHash -and $info.Sha256 -eq $sourceHash.ToLowerInvariant()) 'source stays unchanged'
    $installInfo = [pscustomobject]@{StorageSeed=$seedMetadata.seed}
    Assert-SmokeNativeConsumerSeed -ProjectRoot $temporaryRoot -InstallInfo $installInfo -Manifest $info
    Assert-Case $true 'seed binary archive and public-source-set binding'
    $info.Values.source_sha256='f'*64; $rejected=$false
    try {Assert-SmokeNativeConsumerSeed -ProjectRoot $temporaryRoot -InstallInfo $installInfo -Manifest $info} catch {$rejected=$true}
    Assert-Case $rejected 'wrong seed source identity rejected'; $info.Values.source_sha256=$seedMetadata.source
    $rejected = $false
    try { [void](Copy-SmokeNativeConsumerManifest -InstallDir $install -ManifestPath $path -BinaryPath $binary) } catch {$rejected=$true}
    Assert-Case $rejected 'existing private config preserved'
    Reject-Copy ($manifest -replace $binaryHash, ('f' * 64)) 'wrong-client'
    Reject-Copy ($manifest + "run_id=duplicate`n") 'duplicate'
    Reject-Copy ($manifest + "output_path=../outside`n") 'unknown'
    Reject-Copy ($manifest -replace 'data/model.pdmesh','data/../model.pdmesh') 'traversal'
    Reject-Copy ($manifest -replace '16777216','16777217') 'budget'
    Reject-Copy ('x' * 8193) 'oversized'
    $receiptDir = Join-Path $temporaryRoot 'receipts'; New-Item -ItemType Directory -Path $receiptDir | Out-Null
    Assert-Case (-not (Get-SmokeNativeConsumerResult -Manifest $info -ReceiptDir $receiptDir).Passed) 'missing events pending'
    $common = @{schema='pd2.native-asset-event.v1';run_id='synthetic-helper';attempt_id='model-1';catalog_id='base:model'
        archive_origin='data/model.pdmesh';archive_sha256=('a'*64);operation='modeldef_compile'
        generation=@{binary_sha256=$binaryHash;source_sha256=$seedMetadata.source};timestamp='2026-10-01T11:00:00Z'}
    $begin = $common.Clone(); $begin.event='begin'; $begin.provider='FileProvider'
    $member = $common.Clone(); $member.event='member'; $member.provider='FileProvider'
    $member.member='model.obj'; $member.sha256=('d'*64); $member.bytes=12; $member.source_role='mesh_source'
    $end = $common.Clone(); $end.event='end'; $end.native_completed=$true; $end.rom_fallback=$false
    $end.load_verdict='pass'; $end.checks=@{}; $end.pending_dependencies=@('base:texture_a')
    $lines = @($begin,$member,$end | ForEach-Object {ConvertTo-Json $_ -Depth 5 -Compress}) -join "`n"
    [IO.File]::WriteAllText($info.EventPath, ($lines+"`n"), (New-Object Text.UTF8Encoding($false)))
    $result = Get-SmokeNativeConsumerResult -Manifest $info -ReceiptDir $receiptDir
    Assert-Case ($result.Passed -and $result.Traces -eq 1 -and $result.Fidelity -eq 'pending') 'synthetic conversion preserves fidelity pending'
    $receipt = Get-Content $result.Receipt -Raw | ConvertFrom-Json
    Assert-Case ($receipt.receipts[0].pending_dependencies[0] -eq 'base:texture_a') 'cross-archive sources stay pending'
    Assert-Case ($result.DependencySources -eq 'pending') 'model alone cannot close texture source proof'
    $end.dependency_bindings=@(@{catalog_id='base:texture_a';native_source_closure_sha256=('f'*64);native_slot=65534})
    $textureCommon=$common.Clone(); $textureCommon.attempt_id='texture-1'; $textureCommon.catalog_id='base:texture_a'
    $textureCommon.archive_origin='data/texture.pdtexture'; $textureCommon.archive_sha256='e'*64; $textureCommon.operation='texture_load'
    $textureBegin=$textureCommon.Clone(); $textureBegin.event='begin'; $textureBegin.provider='FileProvider'
    $textureMember=$textureCommon.Clone(); $textureMember.event='member'; $textureMember.provider='FileProvider'
    $textureMember.member='texture.png'; $textureMember.sha256='d'*64; $textureMember.bytes=12; $textureMember.source_role='image_source'
    $textureEnd=$textureCommon.Clone(); $textureEnd.event='end'; $textureEnd.native_completed=$true; $textureEnd.rom_fallback=$false
    $textureEnd.load_verdict='pass'; $textureEnd.checks=@{}; $textureEnd.native_source_closure_sha256='f'*64
    $textureEnd.native_texture=@{native_slot=65534;width=1;height=1;rgba_bytes=4;rgba_sha256=('d'*64)}
    function Write-DependencyFixture {
        $events=@($textureBegin,$textureMember,$textureEnd,$begin,$member,$end)
        $payload=($events | ForEach-Object {ConvertTo-Json $_ -Depth 6 -Compress}) -join "`n"
        [IO.File]::WriteAllText($info.EventPath,($payload+"`n"),(New-Object Text.UTF8Encoding($false)))
    }
    Write-DependencyFixture
    $result=Get-SmokeNativeConsumerResult -Manifest $info -ReceiptDir (Join-Path $temporaryRoot 'dependency-pass')
    Assert-Case ($result.Passed -and $result.Traces -eq 2 -and $result.DependencySources -eq 'pass' -and $result.Fidelity -eq 'pending') 'native parent/leaf join keeps fidelity pending'
    $textureEnd.native_texture.native_slot=65533; Write-DependencyFixture
    Assert-Case (-not (Get-SmokeNativeConsumerResult -Manifest $info -ReceiptDir (Join-Path $temporaryRoot 'wrong-slot')).Passed) 'different retained texture generation rejected'
    $textureEnd.native_texture.native_slot=65534; $textureBegin.archive_sha256='9'*64; $textureMember.archive_sha256='9'*64; $textureEnd.archive_sha256='9'*64; Write-DependencyFixture
    Assert-Case (-not (Get-SmokeNativeConsumerResult -Manifest $info -ReceiptDir (Join-Path $temporaryRoot 'wrong-leaf-archive')).Passed) 'dependency outside frozen source identities rejected'
    $textureBegin.archive_sha256='e'*64; $textureMember.archive_sha256='e'*64; $textureEnd.archive_sha256='e'*64
    $end.checks=@{native_draw='pass'}; Write-DependencyFixture
    Assert-Case (-not (Get-SmokeNativeConsumerResult -Manifest $info -ReceiptDir (Join-Path $temporaryRoot 'fake-draw')).Passed) 'source-only trace cannot promote draw'
    $runner = Join-Path $PSScriptRoot 'run.ps1'; $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($runner,[ref]$tokens,[ref]$errors)
    Assert-Case (-not $errors.Count) 'runner AST'
    $entry=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-SmokeTest'},$true)[0].Extent.Text
    Assert-Case ($entry.Contains('$psi.EnvironmentVariables[''PD_ASSET_CONSUMER_MANIFEST'']') -and
        -not $entry.Contains('$env:PD_ASSET_CONSUMER_MANIFEST =')) 'child-specific environment'
    [pscustomobject]@{Passed=$true;Assertions=$cases;Synthetic=$true;GameLaunched=$false;DesktopProbed=$false;PowerShell=$PSVersionTable.PSVersion.ToString()} | ConvertTo-Json -Compress
} finally {
    $resolved = [IO.Path]::GetFullPath($temporaryRoot)
    if ($resolved -ne $expectedRoot -or -not $resolved.StartsWith(([IO.Path]::GetFullPath([IO.Path]::GetTempPath())), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Owned synthetic temporary cleanup path mismatch.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
