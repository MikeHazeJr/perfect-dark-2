#Requires -Version 5.1
<#
.SYNOPSIS
    Runs a focused Scenario source-only smoke for generated navigation paths.

.DESCRIPTION
    Builds a temporary Test Ash .pdscenario fixture from public source rows:
    five pads, header-only waypoint/waygroup/cover tables, branching public
    navigation/paths.tsv rows, one directional path segment, and a tiny public
    AI list/character plus a public hover_car row that execute quadrant,
    route-to-target, vehicle path, vehicle speed, and cover commands. The smoke
    boots Test Ash with Scenario source-only enabled and requires the runtime
    log proving generated navigation used path edges and segment flags from
    pads.tsv/navigation/paths.tsv and that live AI commands executed against
    the generated runtime nav and cover tables.
#>

[CmdletBinding()]
param(
    [int] $Timeout = 90,
    [string] $SourceBinary = "",
    [string] $SourceRom = "",
    [switch] $KeepGeneratedTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$RunRoot = Join-Path $ProjectRoot ".claude\smoke-verify-runs\scenario-generated-nav-fixture"
$Stamp = "{0:yyyyMMddTHHmmssZ}" -f ((Get-Date).ToUniversalTime())
$GeneratedDir = Join-Path $RunRoot ("tests-{0}" -f $Stamp)
$AssetDir = Join-Path $RunRoot ("assets-{0}" -f $Stamp)
$FixtureDir = Join-Path $AssetDir "fixtures"
$ModDir = Join-Path $AssetDir "scenario_generated_nav_fixture"
$ModScenarioDir = Join-Path $ModDir "scenarios"
$SourceArchive = Join-Path $ProjectRoot "Build\data\ntsc-final\scenarios\base_scenario_test_ash.pdscenario"
$FixtureArchive = Join-Path $ModScenarioDir "base_scenario_test_ash.pdscenario"
$ModsEnabled = Join-Path $FixtureDir "mods-enabled.json"
$Builder = Join-Path $ScriptDir "build_generated_nav_fixture.py"

New-Item -ItemType Directory -Path $GeneratedDir -Force | Out-Null
New-Item -ItemType Directory -Path $FixtureDir -Force | Out-Null
New-Item -ItemType Directory -Path $ModScenarioDir -Force | Out-Null
python $Builder --source $SourceArchive --out $FixtureArchive

function ConvertTo-ProjectRelativePath {
    param([Parameter(Mandatory)] [string] $Path)

    $projectFull = (Resolve-Path -LiteralPath $ProjectRoot).Path.TrimEnd('\', '/')
    $full = (Resolve-Path -LiteralPath $Path).Path
    if (-not $full.StartsWith($projectFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "generated fixture escaped project root: $full"
    }
    return $full.Substring($projectFull.Length).TrimStart('\', '/')
}

$modJson = [ordered]@{
    id = "scenario_generated_nav_fixture"
    name = "Scenario Generated Nav Fixture"
    version = "1.0.0"
    author = "PD2 tests"
    description = "Smoke fixture that overrides base:scenario_test_ash with public pads.tsv, navigation/paths.tsv, generated navigation source, and live AI nav command rows."
}
$modJson | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $ModDir "mod.json") -Encoding UTF8
ConvertTo-Json -InputObject @("scenario_generated_nav_fixture") |
	Set-Content -LiteralPath $ModsEnabled -Encoding UTF8

$modRel = ConvertTo-ProjectRelativePath -Path $ModDir
$modsEnabledRel = ConvertTo-ProjectRelativePath -Path $ModsEnabled
$testPath = Join-Path $GeneratedDir "scenario_generated_nav_path_order.json"
$definition = [ordered]@{
    scenario_name = "scenario_generated_nav_path_order"
    description = "c3844 runtime smoke: boot a Test Ash-derived .pdmod fixture whose header-only nav tables must generate runtime nav rows, path edges, segment flags, waygroups, and live route/vehicle/cover command proof from public pads.tsv plus navigation/paths.tsv."
    tags = @("modding", "pdxxx", "scenario", "source-gate", "navigation", "generated-nav", "c3844", "smoke", "pillar:modding")
    paths_of_interest = @(
        "port/src/scenario_source_runtime.c",
        "tools/smoke-verify/build_generated_nav_fixture.py",
        "tools/smoke-verify/run-scenario-generated-nav-fixture.ps1"
    )
    log_channel_mask = "all"
    verbose = 0
    timeout_seconds = $Timeout
    install_state = "clean"
    fixtures = @(
        [ordered]@{ src = "tools/smoke-verify/fixtures/archive_walker_source_pd.ini"; dst = "pd.ini" },
        [ordered]@{ src = $modsEnabledRel; dst = "mods-enabled.json" }
    )
    packed_fixtures = @(
        [ordered]@{ src = $modRel; dst = "mods/scenario_generated_nav_fixture.pdmod" }
    )
    boot_args = @(
        "--no-update-check",
        "--no-sound",
        "--no-net",
        "--portable",
        "--boot-stage",
        "0x2e"
    )
    input_sequence = @(
        [ordered]@{
            at_ms = 0
            type = "wait"
            comment = "direct Test Ash stage load should consume the staged fixture archive and generate nav from public pads plus paths."
        },
        [ordered]@{
            at_ms = [Math]::Max(10000, ($Timeout - 15) * 1000)
            type = "exit"
            comment = "scripted exit after generated navigation, padfile compilation, and live AI nav command proof have logged."
        }
    )
    assertions = [ordered]@{
        required_lines = @(
            "SMOKE: scenario=scenario_generated_nav_path_order",
            "modmgr: loading mod 'scenario_generated_nav_fixture'",
            "LOAD: lv\.c entering stage load sequence for stagenum=0x2e",
            "SCENARIO\.GRAPH: navigation generate source '.*base_scenario_test_ash\.pdscenario::navigation\.ini' cache=.*base_scenario_test_ash\.pdscenario::_meta/generated-navmesh\.json nodes=1 source_counts=pads:5,volumes:0,waypoints:0,waygroups:0,covers:0,paths:3 source_hashes=sha256 capabilities=walk,jump,drop,wall,ceiling backend=graph\.navigation\.generate\+navigation\.ini\+generated-navmesh\.json",
            "SCENARIO\.SOURCE: generated deterministic navigation tables from public pads\.tsv '.*base_scenario_test_ash\.pdscenario::pads\.tsv' and navigation/paths\.tsv '.*base_scenario_test_ash\.pdscenario::navigation/paths\.tsv' through empty waypoint, waygroup, and cover sources as 5 waypoints, 2 waygroups, 5 covers, 4 path edges, 1 branch waypoints backend=source\.navigation\.generated\+pads\.tsv\+navigation/paths\.tsv\+navigation/waypoints\.tsv\+navigation/waygroups\.tsv\+navigation/covers\.tsv",
            "SCENARIO\.SOURCE: generated directional path segments from public navigation/paths\.tsv '.*base_scenario_test_ash\.pdscenario::navigation/paths\.tsv' as 2 flagged neighbour refs backend=source\.navigation\.generated\+navigation/paths\.tsv\+waypoint-segment-flags",
            "SCENARIO\.SOURCE: compiled setup\.fields\.tsv '.*base_scenario_test_ash\.pdscenario::setup\.fields\.tsv', spawns\.tsv '.*base_scenario_test_ash\.pdscenario::spawns\.tsv', navigation/paths\.tsv '.*base_scenario_test_ash\.pdscenario::navigation/paths\.tsv', and ai/ailists\.tsv '.*base_scenario_test_ash\.pdscenario::ai/ailists\.tsv' for stage '.*' as 2 setup records, \d+ spawns, 3 paths, 2 AI lists \(\d+ bytes\) path_flags=circular:1,flying:1",
            "SCENARIO\.SOURCE: compiled navigation tables waypoints '.*base_scenario_test_ash\.pdscenario::navigation/waypoints\.tsv', waygroups '.*base_scenario_test_ash\.pdscenario::navigation/waygroups\.tsv', covers '.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv' as 5 waypoints, 2 waygroups, 5 covers, waypoint_neighbour_refs=8, waygroup_neighbour_refs=0 backend=source\.navigation\.tables\+navigation/waypoints\.tsv\+navigation/waygroups\.tsv\+navigation/covers\.tsv",
            "SCENARIO\.SOURCE: compiled pads\.tsv '.*base_scenario_test_ash\.pdscenario::pads\.tsv' for stage '.*' as 5 pads, 5 waypoints, 2 waygroups, 5 covers \(\d+ bytes\)",
            "SCENARIO\.GRAPH: AI condition if_waypoint_within_quadrant quadrant=1 label=2 pad=-?\d+ found=\d result=\d pad_rows=5 waypoint_rows=5 waygroup_rows=2 source=.*base_scenario_test_ash\.pdscenario::ai/ailists\.tsv pads=.*base_scenario_test_ash\.pdscenario::pads\.tsv waypoints=.*base_scenario_test_ash\.pdscenario::navigation/waypoints\.tsv waygroups=.*base_scenario_test_ash\.pdscenario::navigation/waygroups\.tsv backend=graph\.ai\.action\.quadrant_preset\+ai/ailists\.tsv\+pads\.tsv\+navigation\.generate",
            "SCENARIO\.GRAPH: AI action set_pad_preset_to_target_quadrant quadrant=1 label=1 pad=-?\d+ found=\d result=\d pad_rows=5 waypoint_rows=5 waygroup_rows=2 source=.*base_scenario_test_ash\.pdscenario::ai/ailists\.tsv pads=.*base_scenario_test_ash\.pdscenario::pads\.tsv waypoints=.*base_scenario_test_ash\.pdscenario::navigation/waypoints\.tsv waygroups=.*base_scenario_test_ash\.pdscenario::navigation/waygroups\.tsv backend=graph\.ai\.action\.quadrant_preset\+ai/ailists\.tsv\+pads\.tsv\+navigation\.generate",
            "SCENARIO\.GRAPH: AI action set_pad_preset_to_pad_on_route_to_target chr_rows=\d+ source_chr=-?\d+ label=3 pad=-?\d+ found=\d pad_rows=5 waypoint_rows=5 waygroup_rows=2 result=\d source=.*base_scenario_test_ash\.pdscenario::ai/ailists\.tsv pads=.*base_scenario_test_ash\.pdscenario::pads\.tsv waypoints=.*base_scenario_test_ash\.pdscenario::navigation/waypoints\.tsv waygroups=.*base_scenario_test_ash\.pdscenario::navigation/waygroups\.tsv paths=.*base_scenario_test_ash\.pdscenario::navigation/paths\.tsv backend=graph\.ai\.condition\.perception_alarm\+ai/ailists\.tsv\+pads\.tsv\+navigation/paths\.tsv",
            "SCENARIO\.GRAPH: AI action hovercar_begin_path path=0 found=1 vehicle_rows=[1-9]\d* truck_type=-?\d+ hovercar_type=\d+ path_rows=3 source=.*base_scenario_test_ash\.pdscenario::navigation/paths\.tsv objects=.*base_scenario_test_ash\.pdscenario::objects\.tsv path_pads=3 pad_rows=5 backend=graph\.ai\.action\.vehicle\+objects\.tsv\+navigation/paths\.tsv\+pads\.tsv",
            "SCENARIO\.GRAPH: AI action set_vehicle_speed vehicle_rows=[1-9]\d* truck_type=-?\d+ hovercar_type=\d+ speedaim=[0-9.]+ speedtime=60\.000 source=.*base_scenario_test_ash\.pdscenario::ai/ailists\.tsv objects=.*base_scenario_test_ash\.pdscenario::objects\.tsv backend=graph\.ai\.action\.vehicle\+ai/ailists\.tsv\+objects\.tsv",
            "SCENARIO\.GRAPH: AI action find_cover chr_rows=\d+ source_chr=-?\d+ criteria=0x8085 refdist=0 assigned=-?\d+ cover_rows=5 source=.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv backend=graph\.ai\.action\.find_cover\+navigation/covers\.tsv",
            "SCENARIO\.GRAPH: AI action find_cover_within_dist chr_rows=\d+ source_chr=-?\d+ criteria=0x8085 refdist=0 assigned=-?\d+ cover_rows=5 source=.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv backend=graph\.ai\.action\.find_cover_within_dist\+navigation/covers\.tsv",
            "SCENARIO\.GRAPH: AI action find_cover_outside_dist chr_rows=\d+ source_chr=-?\d+ criteria=0x8085 refdist=0 assigned=-?\d+ cover_rows=5 source=.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv backend=graph\.ai\.action\.find_cover_outside_dist\+navigation/covers\.tsv",
            "SCENARIO\.GRAPH: AI action go_to_cover chr_rows=\d+ source_chr=-?\d+ speed=17 cover=-?\d+ moved_cover=-?\d+ cover_rows=5 source=.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv backend=graph\.ai\.action\.go_to_cover\+navigation/covers\.tsv",
            "SCENARIO\.GRAPH: AI action check_cover_out_of_sight chr_rows=\d+ source_chr=-?\d+ cover=-?\d+ out_of_sight=\d+ cover_rows=5 source=.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv backend=graph\.ai\.action\.check_cover_out_of_sight\+navigation/covers\.tsv",
            "SCENARIO\.GRAPH: AI action face_cover chr_rows=\d+ source_chr=-?\d+ cover=-?\d+ faced=\d+ cover_rows=5 source=.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv backend=graph\.ai\.action\.face_cover\+navigation/covers\.tsv",
            "SCENARIO\.GRAPH: AI action danger_cover chr_rows=\d+ source_chr=-?\d+ assigned=-?\d+ moved=\d+ cover_rows=5 source=.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv backend=graph\.ai\.action\.danger_cover\+navigation/covers\.tsv",
            "SCENARIO\.GRAPH: AI action release_cover chr_rows=\d+ source_chr=-?\d+ cover=-?\d+ released=\d+ cover_rows=5 source=.*base_scenario_test_ash\.pdscenario::navigation/covers\.tsv backend=graph\.ai\.action\.release_cover\+navigation/covers\.tsv",
            "SMOKE: result=scripted_exit"
        )
        forbidden_patterns = @(
            "EXCEPTION_ACCESS_VIOLATION",
            "ACCESS_VIOLATION",
            "FATAL: ",
            "SMOKE: result=timeout",
            "ASSET\.SOURCE_ONLY",
            "RomProvider:filenum",
            "refusing fallback",
            "SETUP: failed to load pads",
            "SETUP\.PADS: invalid waypoint"
        )
        required_counts = @(
            [ordered]@{ pattern = "SCENARIO\.SOURCE: generated deterministic navigation tables from public pads\.tsv .*navigation/paths\.tsv"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.SOURCE: generated directional path segments from public navigation/paths\.tsv .*2 flagged neighbour refs"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.SOURCE: compiled setup\.fields\.tsv .*2 setup records.*2 AI lists.*path_flags=circular:1,flying:1"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.SOURCE: compiled navigation tables waypoints '.*base_scenario_test_ash\.pdscenario::navigation/waypoints\.tsv'.*waypoint_neighbour_refs=8, waygroup_neighbour_refs=0"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.SOURCE: compiled pads\.tsv '.*base_scenario_test_ash\.pdscenario::pads\.tsv'"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI condition if_waypoint_within_quadrant quadrant=1"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action set_pad_preset_to_target_quadrant quadrant=1"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action set_pad_preset_to_pad_on_route_to_target"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action hovercar_begin_path path=0 found=1 vehicle_rows=[1-9]"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action set_vehicle_speed vehicle_rows=[1-9]"; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action find_cover chr_rows="; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action find_cover_within_dist chr_rows="; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action find_cover_outside_dist chr_rows="; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action go_to_cover chr_rows="; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action check_cover_out_of_sight chr_rows="; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action face_cover chr_rows="; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action danger_cover chr_rows="; min = 1; max = 1 },
            [ordered]@{ pattern = "SCENARIO\.GRAPH: AI action release_cover chr_rows="; min = 1; max = 1 }
        )
    }
}

$definition | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $testPath -Encoding UTF8

$runner = Join-Path $ScriptDir "run.ps1"
$runnerArgs = @(
    "-ExecutionPolicy", "Bypass",
    "-File", $runner,
    "-TestsDir", $GeneratedDir,
    "-Timeout", $Timeout
)
if ($SourceBinary) {
    $runnerArgs += @("-SourceBinary", $SourceBinary)
}
if ($SourceRom) {
    $runnerArgs += @("-SourceRom", $SourceRom)
}

try {
    & powershell @runnerArgs
    exit $LASTEXITCODE
} finally {
	if (-not $KeepGeneratedTests) {
		Remove-Item -LiteralPath $GeneratedDir -Recurse -Force -ErrorAction SilentlyContinue
		Remove-Item -LiteralPath $AssetDir -Recurse -Force -ErrorAction SilentlyContinue
	} else {
		Write-Host ("Generated fixture tests kept at: {0}" -f $GeneratedDir)
		Write-Host ("Generated fixture assets kept at: {0}" -f $AssetDir)
	}
}
