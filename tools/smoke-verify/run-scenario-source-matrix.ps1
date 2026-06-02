#Requires -Version 5.1
<#
.SYNOPSIS
    Runs Scenario source-only stage-load smokes sequentially.

.DESCRIPTION
    Generates temporary smoke definitions from extracted .pdscenario archives and
    feeds them through tools/smoke-verify/run.ps1. Each generated test boots one
    stage with AssetSourceOnlyType=30 and asserts that scene collision, portals,
    pads, and level graph activation came from public .pdscenario members.

    The runner is intentionally sequential because the client has fixed log file
    names. Running multiple stage clients at once would make pd-client.log
    assertions ambiguous.
#>

[CmdletBinding(DefaultParameterSetName = "Representative")]
param(
    [Parameter(ParameterSetName = "Representative")]
    [switch] $Representative,

    [Parameter(ParameterSetName = "All")]
    [switch] $All,

    [Parameter(ParameterSetName = "CatalogIds")]
    [string[]] $CatalogId,

    [Parameter(ParameterSetName = "Stages")]
    [string[]] $Stage,

    [int] $Timeout = 90,
    [string] $SourceBinary = "",
    [string] $SourceRom = "",
    [switch] $KeepGeneratedTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$ScenarioDir = Join-Path $ProjectRoot "Build\data\ntsc-final\scenarios"
$RunRoot = Join-Path $ProjectRoot ".claude\smoke-verify-runs\scenario-source-matrix"
$GeneratedTestsDir = Join-Path $RunRoot ("tests-{0:yyyyMMddTHHmmssZ}" -f ((Get-Date).ToUniversalTime()))

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function ConvertTo-MatrixStageValue {
    [CmdletBinding()] param([Parameter(Mandatory)] [string] $Value)
    $trimmed = $Value.Trim()
    if ($trimmed.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt32($trimmed.Substring(2), 16)
    }
    return [Convert]::ToInt32($trimmed, 10)
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

function Get-ScenarioArchives {
    if (-not (Test-Path -LiteralPath $ScenarioDir)) {
        throw "Scenario archive directory not found: $ScenarioDir. Run extraction/build first."
    }

    $archives = Get-ChildItem -LiteralPath $ScenarioDir -Filter "*.pdscenario" -File |
        Sort-Object Name
    $out = @()

    foreach ($archive in $archives) {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($archive.FullName)
        try {
            $manifestText = Get-ZipEntryText -Zip $zip -Name "_meta/manifest.json"
            if (-not $manifestText) {
                throw "Missing _meta/manifest.json in $($archive.FullName)"
            }
            $manifest = $manifestText | ConvertFrom-Json
            if ($manifest.PSObject.Properties.Match("id").Count -eq 0 -or
                    $manifest.PSObject.Properties.Match("stagenum").Count -eq 0) {
                throw "Scenario manifest lacks id/stagenum: $($archive.FullName)"
            }
            $id = [string]$manifest.id
            $stagenum = [int]$manifest.stagenum
            if ($stagenum -lt 0) { continue }
            $display = ""
            if ($manifest.PSObject.Properties.Match("display_name").Count -gt 0) {
                $display = [string]$manifest.display_name
            }
            $out += [PSCustomObject]@{
                Id = $id
                Stage = $stagenum
                StageHex = ("0x{0:x}" -f $stagenum)
                DisplayName = $display
                ArchiveName = $archive.Name
                ArchivePath = $archive.FullName
            }
        } finally {
            $zip.Dispose()
        }
    }
    return $out
}

function Select-ScenarioMatrixEntries {
    [CmdletBinding()] param([Parameter(Mandatory)] [array] $Archives)

    if ($All) { return @($Archives) }

    if ($CatalogId -and $CatalogId.Count -gt 0) {
        $wanted = @{}
        foreach ($id in $CatalogId) { $wanted[$id] = $true }
        return @($Archives | Where-Object { $wanted.ContainsKey($_.Id) })
    }

    if ($Stage -and $Stage.Count -gt 0) {
        $wantedStages = @{}
        foreach ($s in $Stage) {
            $wantedStages[(ConvertTo-MatrixStageValue -Value $s)] = $true
        }
        return @($Archives | Where-Object { $wantedStages.ContainsKey($_.Stage) })
    }

    $representativeIds = @(
        "base:scenario_chicago",
        "base:scenario_citraining",
        "base:scenario_airbase",
        "base:scenario_test_ash"
    )
    $wantedRepresentative = @{}
    foreach ($id in $representativeIds) { $wantedRepresentative[$id] = $true }
    return @($Archives | Where-Object { $wantedRepresentative.ContainsKey($_.Id) })
}

function New-ScenarioMatrixTest {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [psobject] $Entry,
        [Parameter(Mandatory)] [string] $OutDir
    )

    $slug = ($Entry.Id -replace '^base:scenario_', '' -replace '[^A-Za-z0-9_]+', '_').ToLowerInvariant()
    $testName = "scenario_source_matrix_$slug"
    $idRegex = [regex]::Escape($Entry.Id)
    $archiveRegex = [regex]::Escape($Entry.ArchiveName)
    $stageHexRegex = ("0x0*{0:x}" -f [int]$Entry.Stage)

    $definition = [ordered]@{
        scenario_name = $testName
        description = "c3844 runtime matrix smoke: boot $($Entry.Id) with Scenario source-only enabled and prove core stage-load products come from public .pdscenario source."
        tags = @("modding", "pdxxx", "scenario", "source-gate", "stage-load", "source-matrix", "c3844", "smoke", "pillar:modding")
        paths_of_interest = @(
            "port/src/scenario_source_runtime.c",
            "port/src/loader_walker_scenario.c",
            "src/game/setup.c",
            "src/lib/collision.c",
            "src/game/player.c",
            "tools/smoke-verify/run-scenario-source-matrix.ps1",
            "tools/smoke-verify/fixtures/archive_walker_source_pd.ini"
        )
        log_channel_mask = "all"
        verbose = 0
        timeout_seconds = $Timeout
        install_state = "clean"
    # archive_walker_source_pd.ini carries AssetSourceOnlyType=30, so every
    # generated test refuses Scenario runtime loads that leave public source.
    # Boundary coverage: base:scenario_extra25 (0x5e) and base:scenario_extra26
    # (0x5f) must remain bootable; stale boot-stage clamps broke this path.
        fixtures = @(
            [ordered]@{ src = "tools/smoke-verify/fixtures/archive_walker_source_pd.ini"; dst = "pd.ini" },
            [ordered]@{ src = "tools/smoke-verify/fixtures/no_mods_enabled.json"; dst = "mods-enabled.json" }
        )
        boot_args = @(
            "--no-update-check",
            "--no-sound",
            "--no-net",
            "--portable",
            "--boot-stage",
            $Entry.StageHex
        )
        input_sequence = @(
            [ordered]@{
                at_ms = 0
                type = "wait"
                comment = "direct stage load should bind $($Entry.Id) and compile core runtime products from public .pdscenario source."
            },
            [ordered]@{
                at_ms = [Math]::Max(10000, ($Timeout - 15) * 1000)
                type = "exit"
                comment = "scripted exit after source-owned scene collision, portals, pads, and graph activation have logged."
            }
        )
        assertions = [ordered]@{
            required_lines = @(
                "SMOKE: scenario=$testName",
                ("LOAD: lv\.c entering stage load sequence for stagenum={0}" -f $stageHexRegex),
                ("SCENARIO\.SOURCE: added scene colmesh '{0}' tris=\d+ source=.*{1}::collision\.obj" -f $idRegex, $archiveRegex),
                ("SCENARIO\.SOURCE: compiled portals\.tsv '.*{0}::portals\.tsv' for stage '.*' as \d+ portals \(\d+ bytes\)" -f $archiveRegex),
                ("SCENARIO\.SOURCE: built native portal tables '{0}' portals=\d+ room_refs=\d+ source=portals\.tsv" -f $idRegex),
                ("SCENARIO\.GRAPH: activated level graph '{0}' path=.*{1}::level\.graph\.json bytes=\d+ stage='.*'" -f $idRegex, $archiveRegex),
                ("SCENARIO\.GRAPH: table refs '{0}' pads=.*{1}::pads\.tsv spawns=.*{1}::spawns\.tsv setup=.*{1}::setup\.fields\.tsv ai=.*{1}::ai/ailists\.tsv objects=.*{1}::objects\.tsv volumes=.*{1}::volumes\.tsv objectives=.*{1}::objectives\.tsv waypoints=.*{1}::navigation/waypoints\.tsv waygroups=.*{1}::navigation/waygroups\.tsv covers=.*{1}::navigation/covers\.tsv paths=.*{1}::navigation/paths\.tsv" -f $idRegex, $archiveRegex),
                ("SCENARIO\.SOURCE: compiled pads\.tsv '.*{0}::pads\.tsv' for stage '.*' as \d+ pads, \d+ waypoints, \d+ waygroups, \d+ covers \(\d+ bytes\)" -f $archiveRegex)
            )
            forbidden_patterns = @(
                "EXCEPTION_ACCESS_VIOLATION",
                "ACCESS_VIOLATION",
                "FATAL: ",
                "SMOKE: result=timeout",
                "ASSET\.SOURCE_ONLY",
                "RomProvider:filenum",
                "refusing fallback",
                "GROUNDSNAP: ignoring invalid chr->ground",
                "SETUP: failed to load pads",
                "SETUP\.PADS: invalid waypoint"
            )
            required_counts = @(
                [ordered]@{ pattern = ("SCENARIO\.SOURCE: added scene colmesh '{0}'" -f $idRegex); min = 1; max = 1 },
                [ordered]@{ pattern = ("SCENARIO\.SOURCE: compiled portals\.tsv '.*{0}::portals\.tsv'" -f $archiveRegex); min = 1; max = 1 },
                [ordered]@{ pattern = ("SCENARIO\.SOURCE: built native portal tables '{0}'" -f $idRegex); min = 1; max = 1 },
                [ordered]@{ pattern = ("SCENARIO\.GRAPH: activated level graph '{0}'" -f $idRegex); min = 1; max = 1 }
            )
        }
    }

    $path = Join-Path $OutDir "$testName.json"
    $definition | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

$archives = Get-ScenarioArchives
$selected = @(Select-ScenarioMatrixEntries -Archives $archives)
if ($selected.Count -eq 0) {
    throw "No scenario archives matched the requested matrix selection."
}

New-Item -ItemType Directory -Path $GeneratedTestsDir -Force | Out-Null
foreach ($entry in $selected) {
    [void](New-ScenarioMatrixTest -Entry $entry -OutDir $GeneratedTestsDir)
}

Write-Host ("Scenario source matrix selected {0} of {1} archives." -f $selected.Count, $archives.Count) -ForegroundColor Cyan
foreach ($entry in $selected) {
    Write-Host ("  - {0} ({1})" -f $entry.Id, $entry.StageHex) -ForegroundColor Gray
}

$runner = Join-Path $ScriptDir "run.ps1"
$runnerArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", $runner,
    "-TestsDir", $GeneratedTestsDir,
    "-Timeout", $Timeout
)
if ($SourceBinary) {
    $runnerArgs += @("-SourceBinary", $SourceBinary)
}
if ($SourceRom) {
    $runnerArgs += @("-SourceRom", $SourceRom)
}

& powershell.exe @runnerArgs
$rc = $LASTEXITCODE

if (-not $KeepGeneratedTests) {
    Remove-Item -LiteralPath $GeneratedTestsDir -Recurse -Force -ErrorAction SilentlyContinue
}

exit $rc
