#Requires -Version 5.1
<#
.SYNOPSIS
    Runs exhaustive non-Scenario typed-archive source-only catalog smokes.

.DESCRIPTION
    Generates temporary smoke definitions from extracted public typed archives and
    feeds them through tools/smoke-verify/run.ps1. Each generated test requests a
    batch of catalog loads with --debug-load-catalog-assets-source-only, so every
    selected archive must bind a public FileProvider source before runtime load.

    Scenario is intentionally excluded here; use run-scenario-source-matrix.ps1
    for stage-load parity/source checks.
#>

[CmdletBinding(DefaultParameterSetName = "Representative")]
param(
    [Parameter(ParameterSetName = "Representative")]
    [switch] $Representative,

    [Parameter(ParameterSetName = "All")]
    [switch] $All,

    [Parameter(ParameterSetName = "Families")]
    [string[]] $Family,

    [Parameter(ParameterSetName = "CatalogIds")]
    [string[]] $CatalogId,

    [int] $BatchSize = 20,
    [int] $Timeout = 180,
    [string] $Install = "",
    [string] $SourceBinary = "",
    [string] $SourceRom = "",
    [switch] $KeepGeneratedTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$DataRoot = Join-Path $ProjectRoot "Build\data\ntsc-final"
$RunRoot = Join-Path $ProjectRoot ".claude\smoke-verify-runs\all-family-source-matrix"
$GeneratedTestsDir = Join-Path $RunRoot ("tests-{0:yyyyMMddTHHmmssZ}" -f ((Get-Date).ToUniversalTime()))

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

if ($BatchSize -lt 1) {
    throw "BatchSize must be at least 1."
}

$ArchiveTypeByExtension = @{
    ".pdweapon"     = @{ Request = "weapon";     Result = "weapon";      Family = "weapon" }
    ".pdprojectile" = @{ Request = "projectile"; Result = "projectile";  Family = "projectile" }
    ".pdentity"     = @{ Request = "entity";     Result = "entity";      Family = "entity" }
    ".pdcharacter"  = @{ Request = "character";  Result = "character";   Family = "character" }
    ".pdhead"       = @{ Request = "head";       Result = "head";        Family = "head" }
    ".pdbody"       = @{ Request = "body";       Result = "body";        Family = "body" }
    ".pdarena"      = @{ Request = "arena";      Result = "arena";       Family = "arena" }
    ".pdmission"    = @{ Request = "mission";    Result = "mission";     Family = "mission" }
    ".pdmesh"       = @{ Request = "model";      Result = "model";       Family = "mesh" }
    ".pdanim"       = @{ Request = "animation";  Result = "animation";   Family = "animation" }
    ".pdtexture"    = @{ Request = "texture";    Result = "texture";     Family = "texture" }
    ".pdmaterial"   = @{ Request = "material";   Result = "material";    Family = "material" }
    ".pdskin"       = @{ Request = "skin";       Result = "skin";        Family = "skin" }
    ".pdeffect"     = @{ Request = "effect";     Result = "effect";      Family = "effect" }
    ".pdprop"       = @{ Request = "prop";       Result = "prop";        Family = "prop" }
    ".pdvehicle"    = @{ Request = "vehicle";    Result = "vehicle";     Family = "vehicle" }
    ".pdsfx"        = @{ Request = "sfx";        Result = "audio";       Family = "sfx" }
    ".pdvoice"      = @{ Request = "voice";      Result = "audio";       Family = "voice" }
    ".pdsong"       = @{ Request = "song";       Result = "audio";       Family = "song" }
    ".pdui"         = @{ Request = "ui";         Result = "ui";          Family = "ui" }
    ".pdfont"       = @{ Request = "font";       Result = "font";        Family = "font" }
    ".pdlang"       = @{ Request = "lang";       Result = "lang";        Family = "lang" }
    ".pdgamemode"   = @{ Request = "gamemode";   Result = "gamemode";    Family = "gamemode" }
    ".pdbotprofile" = @{ Request = "botprofile"; Result = "botprofile"; Family = "botprofile" }
    ".pdhud"        = @{ Request = "hud";        Result = "hud";         Family = "hud" }
    ".pdtheme"      = @{ Request = "theme";      Result = "theme";       Family = "theme" }
}

function Get-ZipEntryText {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [System.IO.Compression.ZipArchive] $Zip,
        [Parameter(Mandatory)] [string] $Name
    )
    $entry = $Zip.GetEntry($Name)
    if (-not $entry) { return "" }
    $reader = New-Object System.IO.StreamReader($entry.Open(), [System.Text.Encoding]::UTF8)
    try {
        return $reader.ReadToEnd()
    } finally {
        $reader.Dispose()
    }
}

function Get-TypedArchiveCatalogId {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $Path)

    $zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $manifestText = Get-ZipEntryText -Zip $zip -Name "_meta/manifest.json"
        if ($manifestText) {
            $manifest = $manifestText | ConvertFrom-Json
            if ($manifest.PSObject.Properties.Match("id").Count -gt 0 -and $manifest.id) {
                return [string]$manifest.id
            }
            if ($manifest.PSObject.Properties.Match("catalog_id").Count -gt 0 -and $manifest.catalog_id) {
                return [string]$manifest.catalog_id
            }
        }

        $rootNames = @(
            "weapon.ini", "projectile.ini", "entity.ini", "character.ini",
            "head.ini", "body.ini", "arena.ini", "mission.ini", "mesh.ini",
            "animation.ini", "texture.ini", "material.ini", "skin.ini",
            "effect.ini", "prop.ini", "vehicle.ini", "sound.ini", "voice.ini",
            "music.ini", "ui.ini", "font.ini", "lang.ini", "gamemode.ini",
            "botprofile.ini", "hud.ini", "theme.ini"
        )
        foreach ($name in $rootNames) {
            $text = Get-ZipEntryText -Zip $zip -Name $name
            if (-not $text) { continue }
            foreach ($line in ($text -split "`r?`n")) {
                if ($line -match '^\s*(catalog_id|id)\s*=\s*(.+?)\s*$') {
                    return $matches[2].Trim().Trim('"')
                }
            }
        }
    } finally {
        $zip.Dispose()
    }

    throw "Could not resolve catalog id from $Path"
}

function Get-PublicTypedArchives {
    if (-not (Test-Path -LiteralPath $DataRoot)) {
        throw "Data root not found: $DataRoot. Run extraction/build first."
    }

    $out = @()
    foreach ($ext in ($ArchiveTypeByExtension.Keys | Sort-Object)) {
        $info = $ArchiveTypeByExtension[$ext]
        $archives = Get-ChildItem -LiteralPath $DataRoot -Filter "*$ext" -File -Recurse |
            Sort-Object FullName
        foreach ($archive in $archives) {
            $id = Get-TypedArchiveCatalogId -Path $archive.FullName
            $out += [PSCustomObject]@{
                Id = $id
                RequestType = [string]$info.Request
                ResultType = [string]$info.Result
                Family = [string]$info.Family
                Extension = $ext
                ArchiveName = $archive.Name
                ArchivePath = $archive.FullName
            }
        }
    }
    return $out
}

function Select-MatrixEntries {
    [CmdletBinding()] param([Parameter(Mandatory)] [array] $Archives)

    if ($All) { return @($Archives) }

    if ($CatalogId -and $CatalogId.Count -gt 0) {
        $wanted = @{}
        foreach ($id in $CatalogId) { $wanted[$id] = $true }
        return @($Archives | Where-Object { $wanted.ContainsKey($_.Id) })
    }

    if ($Family -and $Family.Count -gt 0) {
        $wantedFamily = @{}
        foreach ($f in $Family) { $wantedFamily[$f.ToLowerInvariant()] = $true }
        return @($Archives | Where-Object { $wantedFamily.ContainsKey($_.Family.ToLowerInvariant()) })
    }

    $seen = @{}
    $selected = @()
    foreach ($archive in $Archives) {
        if ($seen.ContainsKey($archive.Family)) { continue }
        $seen[$archive.Family] = $true
        $selected += $archive
    }
    return @($selected | Sort-Object Family, Id)
}

function Split-IntoBatches {
    [CmdletBinding()] param([Parameter(Mandatory)] [array] $Entries)

    $batches = @()
    $current = @()

    foreach ($entry in $Entries) {
        if ($current.Count -ge $BatchSize) {
            $batches += ,@($current)
            $current = @()
        }

        $current += $entry
    }

    if ($current.Count -gt 0) {
        $batches += ,@($current)
    }
    return $batches
}

function New-AllFamilyMatrixTest {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [array] $Entries,
        [Parameter(Mandatory)] [int] $BatchIndex,
        [Parameter(Mandatory)] [string] $OutDir
    )

    $testName = "all_family_source_matrix_batch_{0:000}" -f $BatchIndex
    $assetListPath = Join-Path $OutDir "$testName.assets.txt"
    $tokens = @()
    $requiredLines = @("SMOKE: scenario=$testName")

    foreach ($entry in $Entries) {
        $tokens += ("{0}={1}" -f $entry.RequestType, $entry.Id)
    }
    $requiredLines += ("BOOT: --debug-load-catalog-assets-file loaded {0} line\(s\) from" -f $Entries.Count)
    $requiredLines += "SMOKE: result=scripted_exit"
    $tokens | Set-Content -LiteralPath $assetListPath -Encoding ASCII
    $exitAtMs = [Math]::Max(35000, [Math]::Min(300000, 35000 + ($Entries.Count * 20)))

    $definition = [ordered]@{
        scenario_name = $testName
        BugId = "B-509"
        category = "modding"
        description = "c3844 runtime matrix smoke: source-only catalog load batch $BatchIndex for extracted non-Scenario public typed archives. Missing debug-load request/result blocks are B-509 harness observability failures."
        tags = @("modding", "pdxxx", "source-gate", "source-matrix", "all-family", "c3844", "smoke", "pillar:modding")
        paths_of_interest = @(
            "port/src/main.c",
            "port/src/asset_source_debug.c",
            "port/src/assetcatalog_load.c",
            "tools/smoke-verify/run-all-family-source-matrix.ps1",
            "tools/smoke-verify/fixtures/all_family_source_pd.ini"
        )
        log_channel_mask = "all"
        verbose = 0
        timeout_seconds = $Timeout
        install_state = "clean"
        fixtures = @(
            [ordered]@{ src = "tools/smoke-verify/fixtures/all_family_source_pd.ini"; dst = "pd.ini" },
            [ordered]@{ src = "tools/smoke-verify/fixtures/no_mods_enabled.json"; dst = "mods-enabled.json" }
        )
        boot_args = @(
            "--no-update-check",
            "--no-sound",
            "--no-net",
            "--portable",
            "--debug-load-catalog-assets-source-only",
            "--debug-load-catalog-assets-file",
            $assetListPath
        )
        input_sequence = @(
            [ordered]@{
                at_ms = 0
                type = "wait"
                comment = "catalog bootstrap extracts and walks base typed archives before source-only typed-load requests fire."
            },
            [ordered]@{
                at_ms = $exitAtMs
                type = "exit"
                comment = "exit after all requested typed assets have loaded or emitted source-only failures."
            }
        )
        assertions = [ordered]@{
            required_lines = $requiredLines
            forbidden_patterns = @(
                "EXCEPTION_ACCESS_VIOLATION",
                "ACCESS_VIOLATION",
                "FATAL: ",
                "SMOKE: result=timeout",
                "ASSET\.SOURCE_ONLY",
                "has no public FileProvider source",
                "--debug-load-catalog-assets invalid token",
                "--debug-load-catalog-assets token '.*' missing type=id form",
                "--debug-load-catalog-assets argument too long",
                "result=FAIL",
                "result=MISSING",
                "RomProvider:filenum",
                "refusing fallback"
            )
            required_counts = @(
                [ordered]@{
                    pattern = "BOOT: --debug-load-catalog-assets request type=.* source_only=1"
                    min = $Entries.Count
                    max = $Entries.Count
                },
                [ordered]@{
                    pattern = "BOOT: --debug-load-catalog-assets result type=.* result=OK"
                    min = $Entries.Count
                    max = $Entries.Count
                }
            )
        }
    }

    $path = Join-Path $OutDir "$testName.json"
    $json = $definition | ConvertTo-Json -Depth 12
    [System.IO.File]::WriteAllText($path, $json, (New-Object System.Text.UTF8Encoding($false)))
    return $path
}

$archives = @(Get-PublicTypedArchives)
$selected = @(Select-MatrixEntries -Archives $archives)
if ($selected.Count -eq 0) {
    throw "No non-Scenario typed archives matched the requested matrix selection."
}

$duplicates = @($selected | Group-Object RequestType, Id | Where-Object { $_.Count -gt 1 })
if ($duplicates.Count -gt 0) {
    $first = $duplicates[0].Group[0]
    throw "Duplicate debug-load request in selected archives: $($first.RequestType)=$($first.Id)"
}

$batches = @(Split-IntoBatches -Entries $selected)

New-Item -ItemType Directory -Path $GeneratedTestsDir -Force | Out-Null
for ($i = 0; $i -lt $batches.Count; $i++) {
    [void](New-AllFamilyMatrixTest -Entries $batches[$i] -BatchIndex ($i + 1) -OutDir $GeneratedTestsDir)
}

$manifestPath = Join-Path $RunRoot ("all-family-source-matrix-manifest-{0}.json" -f (Split-Path -Leaf $GeneratedTestsDir))
$batchManifests = @()
for ($i = 0; $i -lt $batches.Count; $i++) {
    $batchEntries = @($batches[$i])
    $batchManifests += [ordered]@{
        batch = $i + 1
        count = $batchEntries.Count
        asset_list_file = (Join-Path $GeneratedTestsDir ("all_family_source_matrix_batch_{0:000}.assets.txt" -f ($i + 1)))
        catalog_ids = @($batchEntries | ForEach-Object { $_.Id })
    }
}
$matrixManifest = [ordered]@{
    generated_tests_dir = $GeneratedTestsDir
    selected_count = $selected.Count
    archive_count = $archives.Count
    batch_count = $batches.Count
    batch_size = $BatchSize
    source_binary = $SourceBinary
    source_rom = $SourceRom
    batches = $batchManifests
}
$matrixManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host ("All-family source matrix selected {0} of {1} non-Scenario archives into {2} batch(es)." -f $selected.Count, $archives.Count, $batches.Count) -ForegroundColor Cyan
Write-Host ("  manifest: {0}" -f $manifestPath) -ForegroundColor Gray
foreach ($group in ($selected | Group-Object Family | Sort-Object Name)) {
    Write-Host ("  - {0}: {1}" -f $group.Name, $group.Count) -ForegroundColor Gray
}

$runner = Join-Path $ScriptDir "run.ps1"
$runnerArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", $runner,
    "-TestsDir", $GeneratedTestsDir,
    "-Timeout", $Timeout
)
if ($Install) {
    $runnerArgs += @("-Install", $Install)
}
if ($SourceBinary) {
    $runnerArgs += @("-SourceBinary", $SourceBinary)
}
if ($SourceRom) {
    $runnerArgs += @("-SourceRom", $SourceRom)
}

& powershell.exe @runnerArgs
$rc = $LASTEXITCODE
if ($rc -ne 0) {
    Write-Host ("All-family source matrix failed; generated batch manifest retained at {0}" -f $manifestPath) -ForegroundColor Yellow
    Write-Host "If request/result counts are zero for a batch, treat it as B-509 harness observability before reclassifying any asset family as failed." -ForegroundColor Yellow
}

if (-not $KeepGeneratedTests) {
    Remove-Item -LiteralPath $GeneratedTestsDir -Recurse -Force -ErrorAction SilentlyContinue
}

exit $rc
