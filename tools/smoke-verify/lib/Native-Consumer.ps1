#Requires -Version 5.1
# Operational evidence controls only. Source assets and caller environment stay read-only.
function Copy-SmokeNativeConsumerManifest {
    param([Parameter(Mandatory)][string] $InstallDir,
          [Parameter(Mandatory)][string] $ManifestPath,
          [Parameter(Mandatory)][string] $BinaryPath)
    Assert-SmokePlainTree -Path $ManifestPath
    Assert-SmokePlainTree -Path $BinaryPath
    $source = [IO.File]::Open($ManifestPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($source.Length -le 0 -or $source.Length -gt 8192) { throw 'Native consumer manifest exceeds8KiB or is empty.' }
        $bytes = New-Object byte[] ([int]$source.Length)
        $read = 0
        while ($read -lt $bytes.Length) {
            $count = $source.Read($bytes, $read, $bytes.Length - $read)
            if (-not $count) { throw 'Native manifest changed during read.' }; $read += $count
        }
    } finally { $source.Dispose() }
    $text = (New-Object Text.UTF8Encoding($false, $true)).GetString($bytes)
    $values = @{}
    foreach ($line in ($text -split "`n")) {
        $line = $line.TrimEnd([char]13)
        if (-not $line) { continue }
        if ($line -notmatch '^([a-z0-9_]+)=(.*)$' -or $values.ContainsKey($Matches[1])) { throw 'Invalid/duplicate native consumer control.' }
        $values[$Matches[1]] = $Matches[2]
    }
    $keys = @('schema','binary_sha256','source_sha256','archive_sha256','catalog_id','archive_origin','run_id','max_output_bytes')
    if ($values.Count -ne 8 -or @($keys | Where-Object {-not $values.ContainsKey($_)}).Count -or
        $values.schema -cne 'pd2.native-asset-consumer-manifest.v1') { throw 'Unsupported native consumer manifest controls.' }
    foreach ($key in @('binary_sha256','source_sha256','archive_sha256')) {
        if ($values[$key] -cnotmatch '^[0-9a-f]{64}$') { throw 'Native manifest needs exact lowercase SHA256 identities.' }
    }
    if ($values.binary_sha256 -cne (Get-FileHash -LiteralPath $BinaryPath -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw 'Native manifest does not identify this private client binary.'
    }
    if ($values.catalog_id.Length -ge 128 -or $values.catalog_id -cnotmatch '^[A-Za-z][A-Za-z0-9_.-]*:[A-Za-z0-9_./-]+$' -or
        $values.run_id -cnotmatch '^[A-Za-z0-9_.-]{1,80}$' -or $values.max_output_bytes -notmatch '^[1-9][0-9]{3,7}$' -or
        [long]$values.max_output_bytes -lt 4096 -or [long]$values.max_output_bytes -gt 16MB) { throw 'Unsafe native consumer identity or budget.' }
    $idPath = ($values.catalog_id -split ':',2)[1]
    if ($idPath.StartsWith('/') -or @($idPath -split '/' | Where-Object {$_ -in @('', '.', '..')}).Count) { throw 'Unsafe native catalog identity.' }
    if ($values.archive_origin.Length -gt 1024 -or $values.archive_origin -match '[\\\x00-\x1f]' -or
        $values.archive_origin -notmatch '(?i)\.pdmesh$') { throw 'Native consumer needs an explicit public model origin.' }
    foreach ($boundary in ($values.archive_origin -split '::')) {
        if ($boundary -match ':' -or $boundary.StartsWith('/') -or
            @($boundary -split '/' | Where-Object {$_ -in @('', '.', '..')}).Count) { throw 'Unsafe native archive boundary.' }
    }
    $destination = Get-SmokeFixtureDestination -InstallDir $InstallDir -RelativePath 'native-asset-consumer.ini'
    $events = Get-SmokeFixtureDestination -InstallDir $InstallDir -RelativePath 'native-asset-events.jsonl'
    if (Test-Path -LiteralPath $events) { throw 'Native event output already exists; preserve it.' }
    $target = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $target.Write($bytes, 0, $bytes.Length); $target.Flush($true) } finally { $target.Dispose() }
    return [pscustomobject]@{
        Path=$destination; EventPath=$events; Values=$values; Bytes=$bytes.Length
        Sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Assert-SmokeNativeConsumerSeed {
    param([Parameter(Mandatory)][string] $ProjectRoot,
          [Parameter(Mandatory)][psobject] $InstallInfo,
          [Parameter(Mandatory)][psobject] $Manifest)
    if ($Manifest.Values.archive_origin.Contains('::')) { throw 'This initial smoke handoff supports one direct public model archive; nested owner proof is pending.' }
    $seed = [string]$InstallInfo.StorageSeed
    if ($seed -cnotmatch '^[0-9a-f]{64}$') { throw 'Native consumer needs this run frozen seed.' }
    $path = Join-Path $ProjectRoot ('.claude/smoke-storage/seeds/' + $seed + '.json.gz')
    Assert-SmokePlainTree -Path $path
    $planner = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'asset-inspection-plan.py'
    $code = @'
import importlib.util,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('native_seed',sys.argv[1]); m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
files=m.seed_files(Path(sys.argv[2]),sys.argv[3])
if m.source_set_hash(files)!=sys.argv[4]: raise ValueError('public source-set identity mismatch')
if files.get('PerfectDark.exe',{}).get('sha256')!=sys.argv[5]: raise ValueError('seed client identity mismatch')
if files.get(sys.argv[6],{}).get('sha256')!=sys.argv[7]: raise ValueError('selected public archive seed identity mismatch')
print('Native consumer seed identities verified; no native behavior asserted.')
'@
    $python = Get-SmokeStoragePython
    $messages = @(& $python -B -c $code $planner $path $seed $Manifest.Values.source_sha256 $Manifest.Values.binary_sha256 `
        $Manifest.Values.archive_origin $Manifest.Values.archive_sha256 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Native frozen seed verification failed: $($messages -join ' ')" }
    $Manifest | Add-Member -NotePropertyName SeedManifestPath -NotePropertyValue $path -Force
    $Manifest | Add-Member -NotePropertyName SeedId -NotePropertyValue $seed -Force
}

function Get-SmokeNativeConsumerResult {
    param([Parameter(Mandatory)][psobject] $Manifest,
          [Parameter(Mandatory)][string] $ReceiptDir)
    try {
        Assert-SmokePlainTree -Path $Manifest.EventPath
        $file = Get-Item -LiteralPath $Manifest.EventPath
        if ($file.Length -le 0 -or $file.Length -gt [long]$Manifest.Values.max_output_bytes) {
            throw 'Native event stream empty or over budget.'
        }
        $output = Join-Path $ReceiptDir 'native-consumer'
        Assert-SmokePlainTree -Path $output
        $converter = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'asset-inspection-receipts.py'
        $python = Get-SmokeStoragePython
        $messages = @(& $python -B $converter $Manifest.EventPath --binary-sha256 $Manifest.Values.binary_sha256 `
            --source-sha256 $Manifest.Values.source_sha256 --output $output `
            --selected-catalog-id $Manifest.Values.catalog_id --selected-origin $Manifest.Values.archive_origin `
            --selected-archive-sha256 $Manifest.Values.archive_sha256 `
            --frozen-seed $Manifest.SeedManifestPath --seed-id $Manifest.SeedId 2>&1)
        if ($LASTEXITCODE -ne 0) { throw "Native attribution remains pending: $($messages -join ' ')" }
        $envelopePath = Join-Path $output 'receipts.json'
        $envelope = Get-Content -LiteralPath $envelopePath -Raw | ConvertFrom-Json
        if (-not @($envelope.receipts).Count -or @($envelope.pending).Count) { throw 'No complete native member receipt.' }
        foreach ($receipt in @($envelope.receipts)) {
            if ($receipt.load_verdict -cne 'pass') {
                throw 'Native attribution differs from selected manifest or reports failure.'
            }
        }
        return [pscustomobject]@{Passed=$true; Receipt=$envelopePath; ManifestSha256=$Manifest.Sha256
            EventSha256=(Get-FileHash -LiteralPath $Manifest.EventPath -Algorithm SHA256).Hash.ToLowerInvariant()
            EventBytes=$file.Length; Traces=@($envelope.receipts).Count; Fidelity='pending'
            DependencySources=$envelope.selection.dependency_sources; Failure=$null}
    } catch {
        return [pscustomobject]@{Passed=$false; ManifestSha256=$Manifest.Sha256; Fidelity='pending'; Failure=$_.Exception.Message}
    }
}
