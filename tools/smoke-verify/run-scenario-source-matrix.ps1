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
    [string] $Install = "",
    [string] $SourceBinary = "",
    [string] $SourceRom = "",
    [switch] $KeepGeneratedTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$ScenarioDir = Join-Path $ProjectRoot "Build\data\ntsc-final\scenarios"
$MissionDir = Join-Path $ProjectRoot "Build\data\ntsc-final\missions"
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

function Count-JsonRows {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [System.IO.Compression.ZipArchive] $Zip,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $ArchivePath,
        [Parameter(Mandatory)] [string] $Schema
    )

    $text = Get-ZipEntryText -Zip $Zip -Name $Name
    if (-not $text) {
        throw "Missing $Name in $ArchivePath"
    }

    $doc = $text | ConvertFrom-Json
    if ($doc.schema -ne $Schema) {
        throw "Unexpected JSON schema in $Name for $ArchivePath"
    }
    if ($doc.PSObject.Properties.Match("rows").Count -eq 0) {
        throw "Missing JSON rows in $Name for $ArchivePath"
    }
    return @($doc.rows).Count
}

function Count-JsonListRefs {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [System.IO.Compression.ZipArchive] $Zip,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Property,
        [Parameter(Mandatory)] [string] $ArchivePath,
        [Parameter(Mandatory)] [string] $Schema
    )

    $text = Get-ZipEntryText -Zip $Zip -Name $Name
    if (-not $text) {
        throw "Missing $Name in $ArchivePath"
    }

    $doc = $text | ConvertFrom-Json
    if ($doc.schema -ne $Schema) {
        throw "Unexpected JSON schema in $Name for $ArchivePath"
    }
    if ($doc.PSObject.Properties.Match("rows").Count -eq 0) {
        throw "Missing JSON rows in $Name for $ArchivePath"
    }

    $refs = 0
    foreach ($row in @($doc.rows)) {
        if ($null -eq $row -or $row.PSObject.Properties.Match($Property).Count -eq 0) {
            continue
        }
        foreach ($item in @($row.$Property)) {
            if ($null -ne $item -and ([string]$item).Trim().Length -gt 0) {
                $refs += 1
            }
        }
    }
    return $refs
}

function Count-GeneratedPathWaypointNeighbourRefs {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [System.IO.Compression.ZipArchive] $Zip,
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $ArchivePath
    )

    $text = Get-ZipEntryText -Zip $Zip -Name $Name
    if (-not $text) {
        throw "Missing $Name in $ArchivePath"
    }

    $doc = $text | ConvertFrom-Json
    if ($doc.schema -ne "pd2.scenario.paths.v1") {
        throw "Unexpected paths JSON schema in $Name for $ArchivePath"
    }
    $edges = @{}
    foreach ($row in @($doc.rows)) {
        $flags = 0
        if ($row.PSObject.Properties.Match("flags").Count -gt 0) {
            $flagText = [string]$row.flags
            if ($flagText.Length -gt 0) {
                if ($flagText.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
                    $flags = [Convert]::ToInt32($flagText.Substring(2), 16)
                } else {
                    $flags = [Convert]::ToInt32($flagText, 10)
                }
            }
        }

        $pads = @()
        foreach ($rawPart in @($row.pads)) {
            $part = ([string]$rawPart).Trim()
            if ($part.Length -eq 0) {
                continue
            }
            $pipe = $part.IndexOf("|", [System.StringComparison]::Ordinal)
            if ($pipe -ge 0) {
                $part = $part.Substring(0, $pipe).Trim()
            }
            if ($part.Length -gt 0) {
                $pads += $part
            }
        }

        for ($i = 0; $i + 1 -lt $pads.Count; $i++) {
            if ($pads[$i] -eq $pads[$i + 1]) {
                continue
            }
            $pair = @($pads[$i], $pads[$i + 1]) | Sort-Object
            $edges[("{0}|{1}" -f $pair[0], $pair[1])] = $true
        }
        if (($flags -band 0x01) -ne 0 -and $pads.Count -gt 2 -and
                $pads[0] -ne $pads[$pads.Count - 1]) {
            $pair = @($pads[0], $pads[$pads.Count - 1]) | Sort-Object
            $edges[("{0}|{1}" -f $pair[0], $pair[1])] = $true
        }
    }

    return $edges.Count * 2
}

function Get-MissionObjectiveSummary {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ArchivePath
    )

    $summary = [ordered]@{
        ObjectiveRows = 0
        CriteriaRows = 0
        HasMissionFlagCriteria = $false
        HasObjectStateCriteria = $false
    }

    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        return [PSCustomObject]$summary
    }

    $zip = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $text = Get-ZipEntryText -Zip $zip -Name "objectives.json"
        if (-not $text) {
            throw "Missing objectives.json in $ArchivePath"
        }

        $doc = $text | ConvertFrom-Json
        if ($doc.schema -ne "pd2.scenario.objectives.v1") {
            throw "Unexpected objectives.json schema in $ArchivePath"
        }
        foreach ($row in @($doc.rows)) {
            if (-not $row.kind) {
                throw "Malformed objectives.json row in $ArchivePath"
            }
            $kind = [string]$row.kind
            if ($kind -eq "objective") {
                $summary.ObjectiveRows += 1
            } elseif ($kind.Length -gt 0) {
                $summary.CriteriaRows += 1
            }

            if ($kind -eq "objective_complete_flags" -or
                    $kind -eq "objective_fail_flags") {
                $summary.HasMissionFlagCriteria = $true
            }
            if ($kind -eq "objective_collect_object" -or
                    $kind -eq "objective_destroy_object" -or
                    $kind -eq "objective_throw_object" -or
                    $kind -eq "objective_holograph") {
                $summary.HasObjectStateCriteria = $true
            }
        }
    } finally {
        $zip.Dispose()
    }

    return [PSCustomObject]$summary
}

function Get-ScenarioSetupSummary {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [System.IO.Compression.ZipArchive] $Zip,
        [Parameter(Mandatory)] [string] $ArchivePath
    )

    $summary = [ordered]@{
        SetupBehaviorLinkRows = 0
        SetupBehaviorLinkKinds = @()
    }
    $text = Get-ZipEntryText -Zip $Zip -Name "setup.fields.json"
    if (-not $text) {
        throw "Missing setup.fields.json in $ArchivePath"
    }

    $linkKinds = @{}
    foreach ($kind in @(
            "linked_guns",
            "lift_door_link",
            "safe_item",
            "padlocked_door",
            "conditional_scenery",
            "blocked_path")) {
        $linkKinds[$kind] = $true
    }

    $seenKinds = @{}
    try {
        $parsed = $text | ConvertFrom-Json
    } catch {
        throw "Malformed setup.fields.json in $ArchivePath`: $($_.Exception.Message)"
    }
    if ($parsed.schema -ne "pd2.scenario.setup.fields.v1") {
        throw "Missing pd2.scenario.setup.fields.v1 schema in setup.fields.json for $ArchivePath"
    }
    if ($null -eq $parsed.rows) {
        throw "Missing rows array in setup.fields.json for $ArchivePath"
    }

    foreach ($row in @($parsed.rows)) {
        if ($null -eq $row.kind) {
            throw "Malformed setup.fields.json row in $ArchivePath"
        }
        if ($linkKinds.ContainsKey([string]$row.kind)) {
            $summary.SetupBehaviorLinkRows += 1
            $seenKinds[[string]$row.kind] = $true
        }
    }

    $summary.SetupBehaviorLinkKinds = @($seenKinds.Keys | Sort-Object)
    return [PSCustomObject]$summary
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
            if ($manifest.PSObject.Properties.Match("id").Count -eq 0) {
                throw "Scenario manifest lacks catalog id: $($archive.FullName)"
            }
            $id = [string]$manifest.id
            if ($manifest.PSObject.Properties.Match("stagenum").Count -eq 0) {
                throw "Scenario manifest lacks stagenum metadata for catalog id ${id}: $($archive.FullName)"
            }
            $stagenum = [int]$manifest.stagenum
            if ($stagenum -lt 0) { continue }
            $kind = ""
            if ($manifest.PSObject.Properties.Match("kind").Count -gt 0) {
                $kind = [string]$manifest.kind
            }
            $aiCommandRows = 0
            if ($manifest.PSObject.Properties.Match("ai_command_count").Count -gt 0) {
                $aiCommandRows = [int]$manifest.ai_command_count
            }
            $display = ""
            if ($manifest.PSObject.Properties.Match("display_name").Count -gt 0) {
                $display = [string]$manifest.display_name
            }
            $waypointRows = Count-JsonRows -Zip $zip -Name "navigation/waypoints.json" -ArchivePath $archive.FullName -Schema "pd2.scenario.waypoints.v1"
            $waygroupRows = Count-JsonRows -Zip $zip -Name "navigation/waygroups.json" -ArchivePath $archive.FullName -Schema "pd2.scenario.waygroups.v1"
            $coverRows = Count-JsonRows -Zip $zip -Name "navigation/covers.json" -ArchivePath $archive.FullName -Schema "pd2.scenario.covers.v1"
            $pathRows = Count-JsonRows -Zip $zip -Name "navigation/paths.json" -ArchivePath $archive.FullName -Schema "pd2.scenario.paths.v1"
            $waypointNeighbourRefs = Count-JsonListRefs -Zip $zip -Name "navigation/waypoints.json" -Property "neighbours" -ArchivePath $archive.FullName -Schema "pd2.scenario.waypoints.v1"
            $waygroupNeighbourRefs = Count-JsonListRefs -Zip $zip -Name "navigation/waygroups.json" -Property "neighbours" -ArchivePath $archive.FullName -Schema "pd2.scenario.waygroups.v1"
            $requiresGeneratedNavigation = ($waypointRows -eq 0 -and $waygroupRows -eq 0 -and $coverRows -eq 0)
            $runtimeWaypointNeighbourRefs = $waypointNeighbourRefs
            $runtimeWaygroupNeighbourRefs = $waygroupNeighbourRefs
            if ($requiresGeneratedNavigation -and $pathRows -gt 0) {
                $runtimeWaypointNeighbourRefs = Count-GeneratedPathWaypointNeighbourRefs -Zip $zip -Name "navigation/paths.json" -ArchivePath $archive.FullName
                $runtimeWaygroupNeighbourRefs = 0
            }
            $setupSummary = Get-ScenarioSetupSummary -Zip $zip -ArchivePath $archive.FullName
            $missionId = ""
            $missionArchiveName = ""
            $hasMissionGraph = $false
            $missionObjectiveRows = 0
            $missionObjectiveCriteriaRows = 0
            $missionHasMissionFlagCriteria = $false
            $missionHasObjectStateCriteria = $false
            $scenarioPrefix = "base:scenario_"
            if ($id.StartsWith($scenarioPrefix, [System.StringComparison]::Ordinal)) {
                $missionSlug = $id.Substring($scenarioPrefix.Length)
                $missionId = "base:mission_$missionSlug"
                $missionArchiveName = "base_mission_$missionSlug.pdmission"
                $missionArchivePath = Join-Path $MissionDir $missionArchiveName
                $hasMissionGraph = Test-Path -LiteralPath $missionArchivePath
                if ($hasMissionGraph) {
                    $missionSummary = Get-MissionObjectiveSummary -ArchivePath $missionArchivePath
                    $missionObjectiveRows = [int]$missionSummary.ObjectiveRows
                    $missionObjectiveCriteriaRows = [int]$missionSummary.CriteriaRows
                    $missionHasMissionFlagCriteria = [bool]$missionSummary.HasMissionFlagCriteria
                    $missionHasObjectStateCriteria = [bool]$missionSummary.HasObjectStateCriteria
                }
            }
            $out += [PSCustomObject]@{
                Id = $id
                ScenarioCatalogId = $id
                ArenaCatalogId = ($id -replace '^base:scenario_', 'base:arena_')
                Kind = $kind
                IsMultiplayer = ($kind -eq "mp")
                AiCommandRows = $aiCommandRows
                StageNum = $stagenum
                StageNumHex = ("0x{0:x}" -f $stagenum)
                DisplayName = $display
                ArchiveName = $archive.Name
                ArchivePath = $archive.FullName
                MissionId = $missionId
                MissionArchiveName = $missionArchiveName
                HasMissionGraph = $hasMissionGraph
                MissionObjectiveRows = $missionObjectiveRows
                MissionObjectiveCriteriaRows = $missionObjectiveCriteriaRows
                MissionHasMissionFlagCriteria = $missionHasMissionFlagCriteria
                MissionHasObjectStateCriteria = $missionHasObjectStateCriteria
                NavigationWaypointRows = $waypointRows
                NavigationWaygroupRows = $waygroupRows
                NavigationCoverRows = $coverRows
                NavigationPathRows = $pathRows
                NavigationWaypointNeighbourRefs = $waypointNeighbourRefs
                NavigationWaygroupNeighbourRefs = $waygroupNeighbourRefs
                NavigationRuntimeWaypointNeighbourRefs = $runtimeWaypointNeighbourRefs
                NavigationRuntimeWaygroupNeighbourRefs = $runtimeWaygroupNeighbourRefs
                SetupBehaviorLinkRows = [int]$setupSummary.SetupBehaviorLinkRows
                SetupBehaviorLinkKinds = @($setupSummary.SetupBehaviorLinkKinds)
                RequiresGeneratedNavigation = $requiresGeneratedNavigation
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
        return @($Archives | Where-Object { $wantedStages.ContainsKey($_.StageNum) })
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
    $arenaRegex = [regex]::Escape($Entry.ArenaCatalogId)
    $archiveRegex = [regex]::Escape($Entry.ArchiveName)
    $missionIdRegex = [regex]::Escape($Entry.MissionId)
    $missionArchiveRegex = [regex]::Escape($Entry.MissionArchiveName)
    $bootArgs = @(
        "--no-update-check",
        "--no-sound",
        "--no-net",
        "--portable",
        "--boot-stage",
        $Entry.ScenarioCatalogId
    )
    $bootComment = "direct stage load should bind $($Entry.Id) and compile core runtime products from public .pdscenario source."
    if ([bool]$Entry.IsMultiplayer) {
        $bootArgs = @(
            "--no-update-check",
            "--no-sound",
            "--no-net",
            "--portable",
            "--launch-mp-room",
            $Entry.ArenaCatalogId,
            "base:combat",
            "1",
            "--debug-auto-start-match"
        )
        $bootComment = "direct match start should bind MP arena $($Entry.ArenaCatalogId) to $($Entry.Id) and compile core runtime products from public .pdscenario source."
    }

    $definition = [ordered]@{
        scenario_name = $testName
        description = "c3844 runtime matrix smoke: boot $($Entry.Id) with Scenario source-only enabled and prove core stage-load products come from public .pdscenario source."
        tags = @("modding", "pdxxx", "scenario", "source-gate", "stage-load", "source-matrix", "c3844", "smoke", "pillar:modding")
        paths_of_interest = @(
            "port/src/scenario_source_runtime.c",
            "port/src/loader_walker_scenario.c",
            "src/game/setup.c",
            "src/game/playerreset.c",
            "src/lib/collision.c",
            "src/game/player.c",
            "tools/smoke-verify/run-scenario-source-matrix.ps1",
            "tools/smoke-verify/fixtures/archive_walker_source_pd.ini"
        )
        log_channel_mask = "all"
        verbose = 0
        timeout_seconds = $Timeout
        install_state = "prefilled"
    # archive_walker_source_pd.ini carries AssetSourceOnlyType=30, so every
    # generated test refuses Scenario runtime loads that leave public source.
    # Boundary coverage: base:scenario_extra25 and base:scenario_extra26 must
    # remain bootable; stale client boot-stage clamps broke this path. The
    # client now accepts .pdscenario catalog ids directly, so the command-line
    # identity, source proof, and archive members all stay catalog-id anchored.
        fixtures = @(
            [ordered]@{ src = "tools/smoke-verify/fixtures/archive_walker_source_pd.ini"; dst = "pd.ini" },
            [ordered]@{ src = "tools/smoke-verify/fixtures/no_mods_enabled.json"; dst = "mods-enabled.json" }
        )
        boot_args = $bootArgs
        input_sequence = @(
            [ordered]@{
                at_ms = 0
                type = "wait"
                comment = $bootComment
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
                "LOAD: lv\.c entering stage load sequence",
                ("SCENARIO\.SOURCE: compiled scene tiles '{0}' for stage '.*' as \d+ rooms, \d+ tiles \(\d+ bytes\) source=.*{1}::collision\.obj" -f $idRegex, $archiveRegex),
                ("SCENARIO\.SOURCE: validated background scene source '{0}' for stage '.*' source=.*{1}::scene\.glb tris=\d+; native BG renderer can consume scene\.glb" -f $idRegex, $archiveRegex),
                ("SCENARIO\.SOURCE: background renderer using native scene source '{0}' for stage '.*' source=.*{1}::scene\.glb tris=\d+" -f $idRegex, $archiveRegex),
                ("SCENARIO\.SOURCE: skipped legacy dynamic-light precompute '{0}' rooms=\d+ portals=\d+ source=scene\.glb" -f $idRegex),
                ("SCENARIO\.SOURCE: built native background room tables '{0}' rooms=\d+ tris=\d+ source=scene\.glb" -f $idRegex),
                ("SCENARIO\.SOURCE: added scene colmesh '{0}' tris=\d+ source=.*{1}::collision\.obj" -f $idRegex, $archiveRegex),
                ("SCENARIO\.SOURCE: compiled portals\.json '.*{0}::portals\.json' for stage '.*' as \d+ portals \(\d+ bytes\)" -f $archiveRegex),
                ("SCENARIO\.SOURCE: built native portal tables '{0}' portals=\d+ room_refs=\d+ source=portals\.json" -f $idRegex),
                ("SCENARIO\.GRAPH: activated level graph '{0}' path=.*{1}::level\.graph\.json bytes=\d+ stage='.*'" -f $idRegex, $archiveRegex),
                ("SCENARIO\.GRAPH: table refs '{0}' portals=.*{1}::portals\.json pads=.*{1}::pads\.json spawns=.*{1}::spawns\.json setup=.*{1}::setup\.fields\.json ai=.*{1}::ai/ailists\.json objects=.*{1}::objects\.json volumes=.*{1}::volumes\.json objectives=.*{1}::objectives\.json waypoints=.*{1}::navigation/waypoints\.json waygroups=.*{1}::navigation/waygroups\.json covers=.*{1}::navigation/covers\.json paths=.*{1}::navigation/paths\.json" -f $idRegex, $archiveRegex),
                ("SCENARIO\.GRAPH: global settings source '.*{0}::level\.graph\.json' nodes=1 scenario='.*' kind='.*' backend=graph\.global\.settings\+level\.graph\.nodes" -f $archiveRegex),
                ("SCENARIO\.GRAPH: level tick from graph source '.*{0}::level\.graph\.json' scenario='.*' kind='.*' reason=lvTick\.start backend=graph\.global\.settings\+level\.tick" -f $archiveRegex),
                ("SCENARIO\.GRAPH: volume source '.*{0}::volumes\.json' volumes=\d+ backend=graph\.trigger\.volumes\+volumes\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: pad source '.*{0}::pads\.json' nodes=\d+ backend=graph\.pads\+pads\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: navigation generate source '.*{0}::navigation\.ini' cache=.*{0}::_meta/generated-navmesh\.json nodes=1 source_counts=pads:\d+,volumes:\d+,waypoints:\d+,waygroups:\d+,covers:\d+,paths:\d+ source_hashes=sha256 capabilities=walk,jump,drop,wall,ceiling backend=graph\.navigation\.generate\+navigation\.ini\+generated-navmesh\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: trigger volume nodes '.*{0}::level\.graph\.json' nodes=\d+ rows=\d+ backend=graph\.trigger\.volumes\+level\.graph\.nodes\+volumes\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI list source '.*{0}::ai/ailists\.json' nodes=\d+ backend=graph\.ai\.lists\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI basic/lifecycle actions '.*{0}::level\.graph\.json' stop=1 kneel=1 surrender=1 fade_out=1 remove_chr=1 backend=graph\.ai\.action\.character_lifecycle\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI combat actions '.*{0}::level\.graph\.json' sidestep=1 jump_out=1 run_sideways=1 attack_walk=1 attack_run=1 attack_roll=1 attack_stand=1 attack_kneel=1 attack_lie=1 if_attack_locked=1 if_attacking=1 modify=1 face_entity=1 apply_gset_damage=1 chr_damage_chr=1 consider_grenade_throw=1 drop_item=1 backend=graph\.ai\.action\.combat\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI target movement actions '.*{0}::level\.graph\.json' run_from_target=1 jog_to_target_prop=1 walk_to_target_prop=1 run_to_target_prop=1 go_to_cover_prop=1 jog_to_chr=1 walk_to_chr=1 run_to_chr=1 backend=graph\.ai\.action\.target_movement\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI perception/alarm actions '.*{0}::level\.graph\.json' hear_alarm=1 patrolling=1 alarm_active=1 gas_active=1 hears_target=1 saw_injury=1 saw_death=1 los_target=1 los_attack_target=1 target_nearly_in_sight=1 nearly_in_targets_sight=1 set_pad_route=1 saw_target_recently=1 heard_target_recently=1 backend=graph\.ai\.condition\.perception_alarm\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI spatial perception conditions '.*{0}::level\.graph\.json' los_chr=1 never_screen=1 on_screen=1 chr_room_screen=1 room_screen=1 target_aiming=1 near_miss=1 suspicious_item=1 check_fov=1 fov_left=1 out_fov_left=1 target_fov=1 target_out_fov=1 dist_lt=1 dist_gt=1 backend=graph\.ai\.condition\.spatial_perception\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI distance perception conditions '.*{0}::level\.graph\.json' chr_pad_lt=1 chr_pad_gt=1 dist_chr_lt=1 dist_chr_gt=1 any_chr_near=1 target_pad_lt=1 target_pad_gt=1 backend=graph\.ai\.condition\.distance_perception\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI room/object/weapon conditions '.*{0}::level\.graph\.json' chr_room=1 target_room=1 chr_object=1 weapon_thrown=1 weapon_on_object=1 chr_weapon=1 gun_unclaimed=1 object_healthy=1 backend=graph\.ai\.condition\.room_object_weapon\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI object interaction actions '.*{0}::level\.graph\.json' activated=1 interact=1 destroy=1 drop_object=1 drop_items=1 drop_weapon=1 give_object=1 move_to_pad=1 backend=graph\.ai\.action\.object_interaction\+ai/ailists\.json\+objects\.json\+pads\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI animation actions '.*{0}::level\.graph\.json' chr_do_animation=1 surprise_one_hand=1 surprise_look_around=1 surprise_surrender=1 backend=graph\.ai\.action\.animation\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI random control '.*{0}::level\.graph\.json' random=1 less_than=1 greater_than=1 backend=graph\.ai\.control\.random\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI debug/no-op actions '.*{0}::level\.graph\.json' print=1 noop=1 backend=graph\.ai\.action\.debug_noop\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI list-control actions '.*{0}::level\.graph\.json' set_list=1 set_return_list=1 set_shot_list=1 return_list=1 set_punch_dodge_list=1 set_shooting_at_me_list=1 set_dark_room_list=1 set_player_dead_list=1 backend=graph\.ai\.action\.list_control\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI pad actions '.*{0}::level\.graph\.json' walk_to_pad=1 run_to_pad=1 backend=graph\.ai\.action\.pad\+pads\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI pad movement actions '.*{0}::level\.graph\.json' jog_to_pad=1 go_to_pad_preset=1 backend=graph\.ai\.action\.pad\+pads\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI pad-preset actions '.*{0}::level\.graph\.json' set_pad_preset=1 chr_set_pad_preset=1 chr_copy_pad_preset=1 backend=graph\.ai\.action\.pad_preset\+pads\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI chr-preset actions '.*{0}::level\.graph\.json' set_chr_preset=1 set_chr_target=1 backend=graph\.ai\.action\.chr_preset\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI morale/alertness actions '.*{0}::level\.graph\.json' set_morale=1 add_morale=1 chr_add_morale=1 subtract_morale=1 set_alertness=1 add_alertness=1 chr_add_alertness=1 subtract_alertness=1 increase_squadron_alertness=1 backend=graph\.ai\.action\.state\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI character conditions '.*{0}::level\.graph\.json' if_num_arghs_less_than=1 if_num_arghs_greater_than=1 if_num_close_arghs_less_than=1 if_num_close_arghs_greater_than=1 if_chr_health_greater_than=1 if_chr_health_less_than=1 if_chr_shield_less_than=1 if_chr_shield_greater_than=1 if_injured=1 if_shield_damaged=1 if_morale_less_than=1 if_morale_less_than_random=1 if_alertness=1 if_chr_alertness_less_than=1 if_alertness_less_than_random=1 backend=graph\.ai\.condition\.character_state\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI lifecycle/perception conditions '.*{0}::level\.graph\.json' if_idle=1 if_stopped=1 if_chr_dead=1 if_chr_death_animation_finished=1 if_chr_knocked_out=1 if_can_see_target=1 backend=graph\.ai\.condition\.lifecycle\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI tuning actions '.*{0}::level\.graph\.json' set_hear_distance=1 set_view_distance=1 set_grenade_probability=1 set_chr_num=1 set_max_damage=1 add_health=1 set_shield=1 set_reaction_speed=1 set_recovery_speed=1 set_accuracy=1 set_dodge_rating=1 set_unarmed_dodge_rating=1 backend=graph\.ai\.action\.tuning\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI action/order actions '.*{0}::level\.graph\.json' set_action=1 set_team_orders=1 retreat=1 set_squadron=1 chr_set_listening=1 try_attack_amount=1 backend=graph\.ai\.action\.orders\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: AI intent/status conditions '.*{0}::level\.graph\.json' not_talking=1 orders=1 has_orders=1 squadron_action=1 chr_listening=1 not_listening=1 injured_target=1 action=1 ammo_less=1 chr_target=1 preset_team=1 human=1 skedar=1 prop_sight=1 remove_prop=1 prop_height=1 set_target=1 preset_target=1 preset_near_self=1 preset_near_pad=1 backend=graph\.ai\.condition\.intent_status\+ai/ailists\.json" -f $archiveRegex),
                ("SCENARIO\.GRAPH: path source '.*{0}::navigation/paths\.json' nodes=\d+ backend=graph\.navigation\.paths\+navigation/paths\.json" -f $archiveRegex),
                "SCENARIO\.GRAPH: AI mission/global actions '.*' if_objective_complete=1 if_objective_failed=1 if_all_objectives_complete=1 if_difficulty_less_than=1 if_difficulty_greater_than=1 if_stage_timer_less_than=1 if_stage_timer_greater_than=1 if_stage_id_less_than=1 if_stage_id_greater_than=1 if_num_players_less_than=1 if_kill_count_greater_than=1 if_num_knocked_out_chrs=1 kill_bond=1 backend=graph\.ai\.action\.mission_global\+ai/ailists\.json\+mission\.graph\.json",
                "SCENARIO\.GRAPH: AI path actions '.*' set_path=1 start_patrol=1 backend=graph\.ai\.action\.path\+navigation/paths\.json",
                "SCENARIO\.GRAPH: AI cover actions '.*' find_cover=1 find_cover_within_dist=1 find_cover_outside_dist=1 go_to_cover=1 check_cover_out_of_sight=1 orbit_target=1 face_cover=1 danger_cover=1 release_cover=1 backend=graph\.ai\.action\.cover\+navigation/covers\.json",
                "SCENARIO\.GRAPH: AI player navigation actions '.*' player_auto_walk=1 if_player_auto_walk_finished=1 backend=graph\.ai\.action\.player_navigation\+ai/ailists\.json\+pads\.json",
                "SCENARIO\.GRAPH: AI quadrant pad-preset actions '.*' waypoint_quadrant=1 target_quadrant=1 backend=graph\.ai\.action\.quadrant_preset\+ai/ailists\.json\+pads\.json\+navigation\.generate",
                "SCENARIO\.GRAPH: AI vehicle motion actions '.*' hovercar_begin_path=1 set_vehicle_speed=1 set_rotor_speed=1 backend=graph\.ai\.action\.vehicle\+navigation/paths\.json",
                "SCENARIO\.GRAPH: AI vehicle/investigation actions '.*' danger_object=1 heli_armed=1 hoverbot_next_step=1 shuffle_investigation=1 set_investigation_pad=1 heli_arm=1 heli_unarm=1 backend=graph\.ai\.action\.vehicle_investigation\+ai/ailists\.json\+objects\.json\+pads\.json",
                "SCENARIO\.GRAPH: AI safety/detection conditions '.*' safety2=1 player_cmp_ar34=1 detect_same_floor=1 detect_enemy=1 safety=1 target_slow=1 target_closer=1 target_away=1 backend=graph\.ai\.condition\.safety_detection\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI misc branch conditions '.*' squadron_dead=1 if_true=1 squadron_count=1 natural_anim=1 y=1 sound_timer=1 target_y_diff=1 backend=graph\.ai\.condition\.misc_branch\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI misc effect actions '.*' chr_explosions=1 tinted_glass=1 rocket=1 blur=1 punch=1 eyespy=1 skedar_pounce=1 obj_pad_distance=1 avoid=1 title_init=1 title_exit=1 sparks=1 dr_caroll_images=1 backend=graph\.ai\.action\.misc_effect\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI quip/setup shuffle actions '.*' say_quip=1 ci_staff_quip=1 ruins_pillars=1 pelagic_switches=1 backend=graph\.ai\.action\.quip_shuffle\+ai/ailists\.json\+objects\.json",
                "SCENARIO\.GRAPH: AI team maintenance actions '.*' set_chr_preset_to_unalerted_teammate=1 rebuild_teams=1 rebuild_squadrons=1 backend=graph\.ai\.action\.team\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI alarm actions '.*' try_start_alarm=1 activate_alarm=1 deactivate_alarm=1 backend=graph\.ai\.action\.alarm\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI flag actions '.*' set_flag=1 unset_flag=1 if_has_flag=1 chr_set_flag=1 chr_unset_flag=1 if_chr_has_flag=1 set_stage_flag=1 unset_stage_flag=1 if_stage_flag_eq=1 backend=graph\.ai\.action\.flags\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI chr/object flag actions '.*' set_chrflag=1 unset_chrflag=1 if_has_chrflag=1 chr_set_chrflag=1 chr_unset_chrflag=1 if_chr_has_chrflag=1 chr_set_hidden_flag=1 chr_unset_hidden_flag=1 if_chr_has_hidden_flag=1 set_obj_flag=1 unset_obj_flag=1 if_obj_has_flag=1 backend=graph\.ai\.action\.chr_object_flags\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI door actions '.*' open_door=1 close_door=1 if_door_state=1 if_object_is_door=1 lock_door=1 unlock_door=1 if_door_locked=1 backend=graph\.ai\.action\.door\+objects\.json",
                "SCENARIO\.GRAPH: AI lift actions '.*' if_lift_stationary=1 lift_go_to_stop=1 if_lift_at_stop=1 activate_lift=1 if_using_lift=1 backend=graph\.ai\.action\.lift\+objects\.json\+pads\.json",
                "SCENARIO\.GRAPH: AI weather actions '.*' configure_rain=1 configure_snow=1 backend=graph\.ai\.action\.weather\+ai/ailists\.json\+scenario\.ini",
                "SCENARIO\.GRAPH: AI sky actions '.*' switch_to_alt_sky=1 set_wind_speed=1 backend=graph\.ai\.action\.sky\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI lighting actions '.*' set_lights=1 backend=graph\.ai\.action\.lighting\+ai/ailists\.json\+pads\.json",
                "SCENARIO\.GRAPH: AI room-flag actions '.*' set_room_flag=1 backend=graph\.ai\.action\.room_flags\+ai/ailists\.json\+scene\.glb",
                "SCENARIO\.GRAPH: AI cutscene visibility actions '.*' show_cutscene_chrs=1 backend=graph\.ai\.action\.cutscene_visibility\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI environment actions '.*' configure_environment=1 backend=graph\.ai\.action\.environment\+ai/ailists\.json\+scenario\.ini\+scene\.glb",
                "SCENARIO\.GRAPH: AI target-distance conditions '.*' if_distance_to_target2_less_than=1 if_distance_to_target2_greater_than=1 backend=graph\.ai\.condition\.target_distance\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI audio actions '.*' speak=1 play_sound=1 assign_sound=1 mute=1 if_channel_free=1 set_volume=1 set_volume_by_distance=1 set_playing=1 repeat_object=1 sound_entity=1 repeat_pad=1 if_volume_less_than=1 play_sound_from_prop=1 play_temporary_primary_track=1 backend=graph\.ai\.action\.audio\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI music track actions '.*' play_x_track=1 stop_x_track=1 play_track_isolated=1 play_default_tracks=1 play_cutscene_track=1 stop_cutscene_track=1 play_temporary_track=1 stop_ambient_track=1 backend=graph\.ai\.action\.music_track\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI player weapon-state actions '.*' chr_draw_weapon=1 chr_draw_weapon_in_cutscene=1 set_player_force_speed=1 chr_set_invincible=1 if_player_is_invincible=1 if_chr_has_no_gun=1 chr_delete_weapon=1 if_trigger_shot_list=1 backend=graph\.ai\.action\.player_weapon_state\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI player cutscene/warp actions '.*' end_level=1 end_cutscene=1 warp_jo_to_pad=1 set_camera_animation=1 if_in_cutscene=1 if_cutscene_button_pressed=1 reorient_for_cutscene_stop=1 warp_jo_to_tag=1 revoke_control=1 grant_control=1 player_fade_in=1 players_fade_out=1 if_colour_fade_complete=1 prepare_warp_orbit=1 begin_warp_latch=1 if_warp_latch_complete=1 backend=graph\.ai\.action\.player_cutscene\+ai/ailists\.json\+pads\.json\+objects\.json",
                "SCENARIO\.GRAPH: AI setup/spawn/equipment actions '.*' spawn_chr_at_pad=1 spawn_chr_at_chr=1 try_equip_weapon=1 try_equip_hat=1 set_obj_image=1 object_do_animation=1 set_door_open=1 backend=graph\.ai\.action\.setup_spawn\+ai/ailists\.json\+pads\.json\+objects\.json",
                "SCENARIO\.GRAPH: AI entity lifecycle/motion actions '.*' duplicate_chr=1 enable_chr=1 disable_chr=1 enable_obj=1 disable_obj=1 chr_move_to_pad=1 chr_set_team=1 damage_chr_by_amount=1 do_preset_animation=1 if_player_chr_portal_distance_less_than=1 if_chr_reposition_valid=1 backend=graph\.ai\.action\.entity_lifecycle\+ai/ailists\.json\+pads\.json\+objects\.json",
                "SCENARIO\.GRAPH: AI gun interaction actions '.*' do_gun_command=1 if_distance_to_gun_less_than=1 recover_gun=1 backend=graph\.ai\.action\.gun_interaction\+ai/ailists\.json\+objects\.json\+scene\.glb",
                "SCENARIO\.GRAPH: AI character property actions '.*' chr_copy_properties=1 backend=graph\.ai\.action\.character_property\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI object-room conditions '.*' if_obj_in_room=1 backend=graph\.ai\.condition\.object_room\+ai/ailists\.json\+objects\.json\+pads\.json\+scene\.glb",
                "SCENARIO\.GRAPH: AI perception conditions '.*' if_player_looking_at_object=1 if_target_is_player=1 backend=graph\.ai\.condition\.perception\+ai/ailists\.json\+objects\.json\+scene\.glb",
                "SCENARIO\.GRAPH: AI character/inventory actions '.*' chr_kill=1 remove_weapon_from_inventory=1 clear_inventory=1 release_object=1 chr_grab_object=1 backend=graph\.ai\.action\.character_inventory\+ai/ailists\.json\+objects\.json",
                "SCENARIO\.GRAPH: AI player-state actions '.*' toggle_p1p2=1 chr_set_p1p2=1 chr_set_cloaked=1 set_autogun_target_team=1 backend=graph\.ai\.action\.player_state\+ai/ailists\.json\+objects\.json",
                "SCENARIO\.GRAPH: AI state/device conditions '.*' if_pouncebits_eq=1 if_training_pc_holographed=1 if_player_using_device=1 backend=graph\.ai\.condition\.state_device\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI teleport/cutscene weapon actions '.*' chr_begin_or_end_teleport=1 if_chr_teleport_full_white=1 chr_set_cutscene_weapon=1 backend=graph\.ai\.action\.teleport_cutscene_weapon\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI cutscene presentation actions '.*' fade_screen=1 if_fade_complete=1 set_chr_hudpiece_visible=1 set_passive_mode=1 chr_set_firing_in_cutscene=1 set_portal_flag=1 backend=graph\.ai\.action\.cutscene_presentation\+ai/ailists\.json\+scene\.glb",
                "SCENARIO\.GRAPH: AI music/mode conditions '.*' if_music_event_queue_is_empty=1 if_coop_mode=1 backend=graph\.ai\.condition\.music_mode\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI pad/reference actions '.*' if_chr_same_floor_distance_to_pad_less_than=1 remove_references_to_chr=1 backend=graph\.ai\.action\.pad_reference\+ai/ailists\.json\+pads\.json",
                "SCENARIO\.GRAPH: AI model-part actions '.*' chr_toggle_model_part=1 obj_set_model_part_visible=1 backend=graph\.ai\.action\.model_part\+ai/ailists\.json\+objects\.json",
                "SCENARIO\.GRAPH: AI object-health actions '.*' if_obj_health_less_than=1 set_obj_health=1 backend=graph\.ai\.action\.object_health\+ai/ailists\.json\+objects\.json",
                "SCENARIO\.GRAPH: AI special-death actions '.*' set_chr_special_death_animation=1 backend=graph\.ai\.action\.special_death\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI room-search actions '.*' set_room_to_search=1 backend=graph\.ai\.action\.room_search\+ai/ailists\.json\+scene\.glb",
                "SCENARIO\.GRAPH: AI savefile flag actions '.*' set_savefile_flag=1 unset_savefile_flag=1 if_savefile_flag_set=1 if_savefile_flag_unset=1 backend=graph\.ai\.action\.savefile_flags\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI timer/countdown actions '.*' restart_timer=1 reset_timer=1 pause_timer=1 resume_timer=1 if_timer_stopped=1 if_timer_greater_than_random=1 if_timer_less_than=1 if_timer_greater_than=1 show_countdown_timer=1 hide_countdown_timer=1 set_countdown_timer=1 stop_countdown_timer=1 start_countdown_timer=1 if_countdown_timer_stopped=1 if_countdown_timer_less_than=1 if_countdown_timer_greater_than=1 backend=graph\.ai\.action\.timer\+ai/ailists\.json",
                "SCENARIO\.GRAPH: AI HUD message actions '.*' show_hudmsg=1 show_hudmsg_middle=1 show_hudmsg_top_middle=1 backend=graph\.ai\.action\.hud\+ai/ailists\.json",
                ("SCENARIO\.SOURCE: compiled setup\.fields\.json '.*{0}::setup\.fields\.json', spawns\.json '.*{0}::spawns\.json', navigation/paths\.json '.*{0}::navigation/paths\.json', and ai/ailists\.json '.*{0}::ai/ailists\.json' for stage '.*' as \d+ setup records, \d+ spawns, \d+ paths, \d+ AI lists \(\d+ bytes\) path_flags=circular:\d+,flying:\d+" -f $archiveRegex),
                ("SCENARIO\.SOURCE: compiled navigation tables waypoints '.*{0}::navigation/waypoints\.json', waygroups '.*{0}::navigation/waygroups\.json', covers '.*{0}::navigation/covers\.json' as \d+ waypoints, \d+ waygroups, \d+ covers, waypoint_neighbour_refs={1}, waygroup_neighbour_refs={2} backend=source\.navigation\.tables\+navigation/waypoints\.json\+navigation/waygroups\.json\+navigation/covers\.json" -f $archiveRegex, [int]$Entry.NavigationRuntimeWaypointNeighbourRefs, [int]$Entry.NavigationRuntimeWaygroupNeighbourRefs),
                ("SCENARIO\.SOURCE: compiled pads\.json '.*{0}::pads\.json' for stage '.*' as \d+ pads, \d+ waypoints, \d+ waygroups, \d+ covers \(\d+ bytes\)" -f $archiveRegex)
            )
            forbidden_patterns = @(
                "EXCEPTION_ACCESS_VIOLATION",
                "ACCESS_VIOLATION",
                "FATAL: ",
                "SMOKE: result=timeout",
                "ASSET\.SOURCE_ONLY",
                "RomProvider:filenum",
                "refusing fallback",
                "SPAWN\.INIT: cdFindGroundInfoAtCyl sentinel",
                "GROUNDSNAP: ignoring invalid chr->ground",
                "SETUP: failed to load pads",
                "SETUP\.PADS: invalid waypoint",
                "SCENARIO\.GRAPH: AI condition if_stage_id_(less_than|greater_than) stagenum=",
                "SCENARIO\.GRAPH: AI action chr_set_cutscene_weapon chr=\d+ weapon=",
                "SCENARIO\.GRAPH: AI action chr_set_cutscene_weapon chr=\d+ weapon_id=[^ ]+ fallback_weapon_id=[^ ]+ applied=",
                "SCENARIO\.GRAPH: AI action speak chr=\d+ text=\d+ audio=",
                "SCENARIO\.GRAPH: AI action play_sound channel=\d+ audio=",
                "SCENARIO\.GRAPH: AI action assign_sound channel=\d+ audio=",
                "SCENARIO\.GRAPH: AI action play_sound_from_prop .* audio=",
                "SCENARIO\.GRAPH: AI action play_repeating_sound_from_pad .* sound=",
                "SCENARIO\.GRAPH: AI action play_temporary_primary_track track=",
                "SCENARIO\.GRAPH: AI action play_x_track reason=\d+ track=",
                "SCENARIO\.GRAPH: AI action play_track_isolated track=",
                "SCENARIO\.GRAPH: AI action play_cutscene_track track=",
                "SCENARIO\.GRAPH: AI action play_temporary_track track=",
                "SCENARIO\.GRAPH: AI action set_camera_animation anim=",
                "SCENARIO\.GRAPH: AI action chr_do_animation chr=\d+ anim=",
                "SCENARIO\.GRAPH: AI action object_do_animation anim=",
                "SCENARIO\.GRAPH: AI action do_preset_animation preset=\d+ applied=",
                "SCENARIO\.GRAPH: AI condition if_natural_anim anim=",
                "SCENARIO\.GRAPH: AI action drop_item chr=\d+ model=",
                "SCENARIO\.GRAPH: AI condition if_player_using_cmp_or_ar34 weapon=",
                "SCENARIO\.GRAPH: AI action chr_draw_weapon chr=\d+ weapon=",
                "SCENARIO\.GRAPH: AI action chr_draw_weapon_in_cutscene chr=\d+ weapon=",
                "SCENARIO\.GRAPH: AI action chr_delete_weapon chr=\d+ weapon=",
                "SCENARIO\.GRAPH: AI action spawn_chr_at_pad body=",
                "SCENARIO\.GRAPH: AI action spawn_chr_at_chr body=",
                "SCENARIO\.GRAPH: AI action try_equip_weapon model=",
                "SCENARIO\.GRAPH: AI action try_equip_hat model=",
                "SCENARIO\.GRAPH: AI action set_obj_image .* image=",
                "SCENARIO\.GRAPH: AI action remove_weapon_from_inventory weapon="
            )
            required_counts = @(
                [ordered]@{ pattern = ("SCENARIO\.SOURCE: added scene colmesh '{0}'" -f $idRegex); min = 1; max = 1 },
                [ordered]@{ pattern = ("SCENARIO\.SOURCE: compiled portals\.json '.*{0}::portals\.json'" -f $archiveRegex); min = 1; max = 1 },
                [ordered]@{ pattern = ("SCENARIO\.SOURCE: built native portal tables '{0}'" -f $idRegex); min = 1; max = 1 },
                [ordered]@{ pattern = ("SCENARIO\.GRAPH: activated level graph '{0}'" -f $idRegex); min = 1; max = 1 }
            )
        }
    }

    if ([bool]$Entry.IsMultiplayer) {
        $definition.assertions.required_lines += @(
            ("BOOT: --launch-mp-room arena='{0}'" -f $arenaRegex),
            "BOOT: --debug-auto-start-match consumed",
            "BOOT: --launch-mp-room direct match start",
            ("MATCHSTART\.DIAG: entry stage_id='{0}'" -f $arenaRegex),
            ("MATCHSETUP: stage '{0}'.*stagenum={1}" -f $arenaRegex, [regex]::Escape($Entry.StageNumHex))
        )
    }

    if ([int]$Entry.AiCommandRows -eq 0) {
        $definition.assertions.required_lines = @(
            $definition.assertions.required_lines |
                Where-Object { $_ -notmatch '^SCENARIO\\\.GRAPH: AI (?!list source)' }
        )
    }

    if ($Entry.RequiresGeneratedNavigation) {
        if ([int]$Entry.NavigationPathRows -gt 0) {
            $definition.assertions.required_lines +=
                ("SCENARIO\.SOURCE: generated deterministic navigation tables from public pads\.json '.*{0}::pads\.json' and navigation/paths\.json '.*{0}::navigation/paths\.json' through empty waypoint, waygroup, and cover sources as \d+ waypoints, \d+ waygroups, \d+ covers, \d+ path edges, \d+ branch waypoints backend=source\.navigation\.generated\+pads\.json\+navigation/paths\.json\+navigation/waypoints\.json\+navigation/waygroups\.json\+navigation/covers\.json" -f $archiveRegex)
        } else {
            $definition.assertions.required_lines +=
                ("SCENARIO\.SOURCE: generated deterministic navigation tables from public pads\.json '.*{0}::pads\.json' through empty waypoint, waygroup, and cover sources as \d+ waypoints, \d+ waygroups, \d+ covers backend=source\.navigation\.generated\+pads\.json\+navigation/waypoints\.json\+navigation/waygroups\.json\+navigation/covers\.json" -f $archiveRegex)
        }
    }

    if ([int]$Entry.SetupBehaviorLinkRows -gt 0) {
        $definition.assertions.required_lines +=
            ("SCENARIO\.GRAPH: setup behavior link source '.*{0}::setup\.fields\.json' links=\d+ backend=graph\.setup\.links\+setup\.fields\.json" -f $archiveRegex)
    }

    if ($Entry.Id -eq "base:scenario_chicago" -or
        $Entry.Id -eq "base:scenario_citraining" -or
        $Entry.Id -eq "base:scenario_extra17" -or
        $Entry.Id -eq "base:scenario_extra25") {
        foreach ($kind in @($Entry.SetupBehaviorLinkKinds)) {
            $definition.assertions.required_lines +=
                ("SCENARIO\.GRAPH: setup behavior link registered from graph source '.*{0}::setup\.fields\.json' record=\d+ kind={1} backend=graph\.setup\.links\+setup\.fields\.json" -f $archiveRegex, [regex]::Escape($kind))
        }
    }

    if ($Entry.Id -eq "base:scenario_chicago" -or
        $Entry.Id -eq "base:scenario_extra17") {
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI action set_path chr_rows=\d+ source_chr=-?\d+ path=\d+ found=1 path_rows=\d+ source=.*{0}::navigation/paths\.json path_pads=\d+ pad_rows=\d+ backend=graph\.ai\.action\.set_path\+navigation/paths\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action start_patrol chr_rows=\d+ source_chr=-?\d+ path=\d+ found=1 path_rows=\d+ source=.*{0}::navigation/paths\.json path_pads=\d+ pad_rows=\d+ backend=graph\.ai\.action\.start_patrol\+navigation/paths\.json\+pads\.json" -f $archiveRegex)
        )
    }

    if ($Entry.Id -eq "base:scenario_extra25") {
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI action go_to_pad_preset chr_rows=\d+ source_chr=-?\d+ pad=\d+ found=1 pad_rows=\d+ source=.*{0}::pads\.json backend=graph\.ai\.action\.go_to_pad_preset\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action spawn_chr_at_pad body_id=[^ ]+ head_id=[^ ]+ pad=\d+ found=1 pad_rows=\d+ ailist=\d+ pass=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.setup_spawn\+ai/ailists\.json\+pads\.json" -f $archiveRegex)
        )
    }

    if ($Entry.Id -eq "base:scenario_extra16") {
        # Extra16's intro sequence naturally exercises source-backed object,
        # lift, lighting, room, distance, and target-movement runtime paths.
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI action object_move_to_pad tag=\d+ object_rows=\d+ pad=\d+ found=1 pad_rows=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.object_interaction\+ai/ailists\.json\+objects\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action activate_lift liftnum=\d+ pad=\d+ found=1 pad_rows=\d+ tag=\d+ object_rows=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.lift\+objects\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_lights pad=\d+ found=1 pad_rows=\d+ chr_rows=\d+ source_chr=-?\d+ room=-?\d+ op=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.lighting\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_chr_distance_to_pad_less_than value=\d+ pad=\d+ found=1 pad_rows=\d+ chr_rows=\d+ source_chr=-?\d+ label=\d+ result=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.condition\.distance_perception\+ai/ailists\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_chr_in_room chr=\d+ room_type=\d+ pad=\d+ found=1 pad_rows=\d+ checked_players=\d+ room=\d+ label=\d+ result=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.condition\.room_object_weapon\+ai/ailists\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action try_jog_to_target_prop chr=\d+ chr_rows=\d+ source_chr=-?\d+ target_chr=-?\d+ target_chr_rows=\d+ resolved_target_chr=-?\d+ label=\d+ result=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.target_movement\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action try_run_to_target_prop chr=\d+ chr_rows=\d+ source_chr=-?\d+ target_chr=-?\d+ target_chr_rows=\d+ resolved_target_chr=-?\d+ label=\d+ result=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.target_movement\+ai/ailists\.json" -f $archiveRegex)
        )
    }

    if ($Entry.Id -eq "base:scenario_airbase") {
        # Airbase carries the retained hovercar route and waypoint/quadrant
        # branch coverage; pin these to the archive, not just any matrix stage.
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI perception/alarm actions '.*{0}::level\.graph\.json' hear_alarm=1 patrolling=1 alarm_active=1 gas_active=1 hears_target=1 saw_injury=1 saw_death=1 los_target=1 los_attack_target=1 target_nearly_in_sight=1 nearly_in_targets_sight=1 set_pad_route=1 saw_target_recently=1 heard_target_recently=1 backend=graph\.ai\.condition\.perception_alarm\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI quadrant pad-preset actions '.*{0}::level\.graph\.json' waypoint_quadrant=1 target_quadrant=1 backend=graph\.ai\.action\.quadrant_preset\+ai/ailists\.json\+pads\.json\+navigation\.generate" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI vehicle motion actions '.*{0}::level\.graph\.json' hovercar_begin_path=1 set_vehicle_speed=1 set_rotor_speed=1 backend=graph\.ai\.action\.vehicle\+navigation/paths\.json" -f $archiveRegex)
        )
    }

    if ($Entry.Id -eq "base:scenario_extra16") {
        # Extra16's retained intro route covers player auto-walk plus vehicle
        # route activation from the public AI/path/pad sources.
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI player navigation actions '.*{0}::level\.graph\.json' player_auto_walk=1 if_player_auto_walk_finished=1 backend=graph\.ai\.action\.player_navigation\+ai/ailists\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI vehicle motion actions '.*{0}::level\.graph\.json' hovercar_begin_path=1 set_vehicle_speed=1 set_rotor_speed=1 backend=graph\.ai\.action\.vehicle\+navigation/paths\.json" -f $archiveRegex)
        )
    }

    if ($Entry.Id -eq "base:scenario_duel") {
        # Duel naturally executes the player auto-walk intro route, so keep
        # this as live source-backed proof instead of activation-only coverage.
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI action player_auto_walk chr=\d+ chr_rows=\d+ target_chr=-?\d+ player_checked=\d+ pad=\d+ found=1 pad_rows=\d+ applied=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.player_navigation\+ai/ailists\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_player_auto_walk_finished chr=\d+ chr_rows=\d+ target_chr=-?\d+ player_checked=\d+ label=\d+ walking=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.player_navigation\+ai/ailists\.json\+pads\.json" -f $archiveRegex)
        )
    }

    if ($Entry.Id -eq "base:scenario_skedarruins") {
        # Skedar Ruins retains the target-quadrant route coverage that would
        # otherwise regress silently behind the generic activation summary.
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI quadrant pad-preset actions '.*{0}::level\.graph\.json' waypoint_quadrant=1 target_quadrant=1 backend=graph\.ai\.action\.quadrant_preset\+ai/ailists\.json\+pads\.json\+navigation\.generate" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI perception/alarm actions '.*{0}::level\.graph\.json' hear_alarm=1 patrolling=1 alarm_active=1 gas_active=1 hears_target=1 saw_injury=1 saw_death=1 los_target=1 los_attack_target=1 target_nearly_in_sight=1 nearly_in_targets_sight=1 set_pad_route=1 saw_target_recently=1 heard_target_recently=1 backend=graph\.ai\.condition\.perception_alarm\+ai/ailists\.json" -f $archiveRegex)
        )
    }

    if ($Entry.Id -eq "base:scenario_citraining") {
        # The retained CI Training route proves these families at graph activation,
        # but does not deterministically execute every savefile/text/preset branch.
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI action set_list target=\d+ chr_rows=\d+ target_chr=-?\d+ list=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.list_control\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action chr_move_to_pad chr=\d+ chr_rows=\d+ target_chr=-?\d+ operand=\d+ mode88_chr=-?\d+ mode88_chr_rows=\d+ resolved_pad=\d+ mode=\d+ pass=\d+ pad_rows=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.entity_lifecycle\+ai/ailists\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_obj_flag tag=\d+ object_rows=\d+ bank=\d+ flags=0x[0-9a-fA-F]+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.set_obj_flag\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_in_cutscene label=\d+ branch=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.player_cutscene\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action end_cutscene source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.player_cutscene\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_chr_distance_to_pad_less_than value=\d+ pad=\d+ found=1 pad_rows=\d+ chr_rows=\d+ source_chr=-?\d+ label=\d+ result=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.condition\.distance_perception\+ai/ailists\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_chr_activated_object chr=\d+ chr_rows=\d+ target_chr=-?\d+ player_checked=\d+ tag=\d+ object_rows=\d+ result=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.object_interaction\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_can_see_target chr_rows=\d+ source_chr=-?\d+ label=\d+ branch=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.condition\.lifecycle\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action random chr_rows=\d+ source_chr=-?\d+ value=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.control\.random\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_random_greater_than chr_rows=\d+ source_chr=-?\d+ threshold=\d+ label=\d+ branch=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.control\.random\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_obj_in_room tag=\d+ object_rows=\d+ pad=\d+ found=1 pad_rows=\d+ resolved_room=\d+ label=\d+ result=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json pads=.*{0}::pads\.json backend=graph\.ai\.condition\.object_room\+ai/ailists\.json\+objects\.json\+pads\.json\+scene\.glb" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI condition if_los_to_target chr_rows=\d+ source_chr=-?\d+ vehicle_rows=\d+ source_vehicle_type=-?\d+ value=\d+ label=\d+ result=\d+ source=.*{0}::ai/ailists\.json objects=.* backend=graph\.ai\.condition\.perception_alarm\+ai/ailists\.json(?:\+objects\.json)?" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action if_obj_has_flag tag=\d+ object_rows=\d+ bank=\d+ flags=0x[0-9a-fA-F]+ result=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.if_obj_has_flag\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_autogun_target_team tag=\d+ object_rows=\d+ team=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.player_state\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action configure_environment room=\d+ room_rows=\d+ command=0x[0-9a-fA-F]+ value=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.environment\+ai/ailists\.json\+scenario\.ini\+scene\.glb" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action stop chr=\d+ chr_rows=\d+ source_chr=-?\d+ hovercar=\d+ vehicle_rows=\d+ source_vehicle_type=-?\d+ source=.*{0}::ai/ailists\.json objects=.* backend=graph\.ai\.action\.basic_motion\+ai/ailists\.json(?:\+objects\.json)?" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action face_entity chr=\d+ chr_rows=\d+ source_chr=-?\d+ type=\d+ id=\d+ label=\d+ result=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.combat\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action return_list chr_rows=\d+ source_chr=-?\d+ vehicle_rows=\d+ source_vehicle_type=-?\d+ list=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.list_control\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action play_sound channel=\d+ audio_id=[^ ]+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.audio\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action toggle_p1p2 chr=\d+ chr_rows=\d+ target_chr=-?\d+ player_checked=\d+ applied=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.player_state\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_passive_mode enable=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.cutscene_presentation\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action chr_set_chrflag chr=\d+ chr_rows=\d+ target_chr=-?\d+ selector=\d+ flags=0x[0-9a-fA-F]+ applied=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.chr_set_chrflag\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_camera_animation anim_id=[^ ]+ anim_source=.*\.pdanim::animation\.gltf clip_bytes=[1-9]\d* player_checked=\d+ chr_rows=\d+ source_chr=-?\d+ yielded=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.player_cutscene\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action play_cutscene_track track_id=[^ ]+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.music_track\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action chr_do_animation chr=\d+ chr_rows=\d+ target_chr=-?\d+ player_checked=\d+ anim_id=[^ ]+ anim_source=.*\.pdanim::animation\.gltf clip_bytes=[1-9]\d* target=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.animation\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action restart_timer target=[a-z]+ chr_rows=\d+ target_chr=-?\d+ vehicle_rows=\d+ source_vehicle_type=-?\d+ source=.*{0}::ai/ailists\.json objects=.* backend=graph\.ai\.action\.restart_timer\+ai/ailists\.json(?:\+objects\.json)?" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action fade_screen color=0x[0-9a-fA-F]+ frames=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.cutscene_presentation\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action if_savefile_flag_unset flag=0x[0-9a-fA-F]+ result=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.if_savefile_flag_unset\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action disable_obj tag=\d+ object_rows=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.entity_lifecycle\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_obj_image tag=\d+ object_rows=\d+ slot=\d+ image_index=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.setup_spawn\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action unset_obj_flag tag=\d+ object_rows=\d+ bank=\d+ flags=0x[0-9a-fA-F]+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.unset_obj_flag\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action activate_lift liftnum=\d+ pad=\d+ found=1 pad_rows=\d+ tag=\d+ object_rows=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.lift\+objects\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_lights pad=\d+ found=1 pad_rows=\d+ chr_rows=\d+ source_chr=-?\d+ room=-?\d+ op=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.lighting\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_morale chr_rows=\d+ source_chr=-?\d+ value=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.set_morale\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action if_timer_greater_than value=[0-9.]+ result=\d+ target=[a-z]+ chr_rows=\d+ target_chr=-?\d+ vehicle_rows=\d+ source_vehicle_type=-?\d+ source=.*{0}::ai/ailists\.json objects=.* backend=graph\.ai\.action\.if_timer_greater_than\+ai/ailists\.json(?:\+objects\.json)?" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action chr_set_team chr=\d+ chr_rows=\d+ target_chr=-?\d+ checked_players=\d+ team=\d+ applied=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.entity_lifecycle\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action if_stage_flag_eq flags=0x[0-9a-fA-F]+ expected=\d+ result=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.if_stage_flag_eq\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action deactivate_alarm source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.deactivate_alarm\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action if_savefile_flag_set flag=0x[0-9a-fA-F]+ result=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.if_savefile_flag_set\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action if_chr_has_hidden_flag chr=\d+ chr_rows=\d+ target_chr=-?\d+ selector=\d+ flags=0x[0-9a-fA-F]+ result=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.if_chr_has_hidden_flag\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action reorient_for_cutscene_stop mode=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.player_cutscene\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action chr_unset_chrflag chr=\d+ chr_rows=\d+ target_chr=-?\d+ selector=\d+ flags=0x[0-9a-fA-F]+ applied=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.chr_unset_chrflag\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action chr_set_hidden_flag chr=\d+ chr_rows=\d+ target_chr=-?\d+ selector=\d+ flags=0x[0-9a-fA-F]+ applied=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.chr_set_hidden_flag\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action print len=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.debug_noop\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_chrflag chr_rows=\d+ target_chr=-?\d+ flags=0x[0-9a-fA-F]+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.set_chrflag\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_return_list target=\d+ chr_rows=\d+ source_chr=-?\d+ target_chr=-?\d+ vehicle_rows=\d+ source_vehicle_type=-?\d+ list=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.list_control\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action if_has_flag chr_rows=\d+ source_chr=-?\d+ flags=0x[0-9a-fA-F]+ bank=\d+ invert=\d+ result=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.if_has_flag\+ai/ailists\.json" -f $archiveRegex)
        )
    }

    if ($Entry.Id -eq "base:scenario_rescue") {
        # Rescue naturally executes a broad source-backed intro route. Keep
        # these as live proof instead of relying only on activation summaries.
        $definition.assertions.required_lines += @(
            ("SCENARIO\.GRAPH: AI action set_path chr_rows=\d+ source_chr=-?\d+ path=\d+ found=1 path_rows=\d+ source=.*{0}::navigation/paths\.json path_pads=\d+ pad_rows=\d+ backend=graph\.ai\.action\.set_path\+navigation/paths\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action start_patrol chr_rows=\d+ source_chr=-?\d+ path=\d+ found=1 path_rows=\d+ source=.*{0}::navigation/paths\.json path_pads=\d+ pad_rows=\d+ backend=graph\.ai\.action\.start_patrol\+navigation/paths\.json\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action run_to_pad chr_rows=\d+ source_chr=-?\d+ pad=\d+ found=1 pad_rows=\d+ source=.*{0}::pads\.json backend=graph\.ai\.action\.run_to_pad\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action give_object_to_chr tag=\d+ chr=\d+ chr_rows=\d+ target_chr=-?\d+ player_checked=\d+ object_rows=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.object_interaction\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action object_do_animation anim_id=[^ ]+ anim_source=.*\.pdanim::animation\.gltf clip_bytes=[1-9]\d* tag=\d+ resolved_tag=\d+ object_rows=\d+ chr_rows=\d+ source_chr=-?\d+ speed_divisor=\d+ startframe=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.setup_spawn\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action lock_door tag=\d+ object_rows=\d+ bits=0x[0-9a-fA-F]+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.door\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action open_door tag=\d+ object_rows=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.door\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_lights pad=\d+ found=1 pad_rows=\d+ chr_rows=\d+ source_chr=-?\d+ room=-?\d+ op=\d+ source=.*{0}::ai/ailists\.json pads=.*{0}::pads\.json backend=graph\.ai\.action\.lighting\+pads\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action configure_environment room=\d+ room_rows=\d+ command=0x[0-9a-fA-F]+ value=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.environment\+ai/ailists\.json\+scenario\.ini\+scene\.glb" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_max_damage chr=\d+ chr_rows=\d+ target_chr=-?\d+ vehicle_rows=\d+ source_vehicle_type=-?\d+ value=[0-9.]+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.set_max_damage\+ai/ailists\.json\+objects\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_shot_list chr_rows=\d+ source_chr=-?\d+ list=\d+ applied=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.list_control\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action speak chr=\d+ chr_rows=\d+ target_chr=-?\d+ target_selector=\d+ player_checked=\d+ text_id=[^ ]+ audio_id=[^ ]+ channel=\d+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.audio\+ai/ailists\.json\+lang" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action assign_sound channel=\d+ audio_id=[^ ]+ source=.*{0}::ai/ailists\.json backend=graph\.ai\.action\.audio\+ai/ailists\.json" -f $archiveRegex),
            ("SCENARIO\.GRAPH: AI action set_object_sound_playing tag=\d+ object_rows=\d+ channel=\d+ timer=\d+ applied=\d+ source=.*{0}::ai/ailists\.json objects=.*{0}::objects\.json backend=graph\.ai\.action\.audio\+ai/ailists\.json\+objects\.json" -f $archiveRegex)
        )
    }

    if ($Entry.HasMissionGraph) {
        $definition.assertions.required_lines += @(
            ("MISSION\.GRAPH: activated mission graph '{0}' scenario='{1}' path=.*{2}::mission\.graph\.json bytes=\d+ backend=parity" -f $missionIdRegex, $idRegex, $missionArchiveRegex),
            ("MISSION\.GRAPH: objective runtime source '.*{0}::objectives\.json' objectives=\d+ criteria=\d+ backend=graph\.objective\.source" -f $missionArchiveRegex),
            ("MISSION\.GRAPH: phase source '.*{0}::mission\.graph\.json' phases=\d+ backend=graph\.mission\.phase\+mission\.graph\.nodes" -f $missionArchiveRegex),
            ("MISSION\.GRAPH: phase transition from graph source '.*{0}::mission\.graph\.json' phase=load reason=mission\.graph\.activate backend=graph\.mission\.phase" -f $missionArchiveRegex),
            ("MISSION\.GRAPH: phase transition from graph source '.*{0}::mission\.graph\.json' phase=active reason=lvTick\.start backend=graph\.mission\.phase" -f $missionArchiveRegex)
        )
        if ([int]$Entry.MissionObjectiveRows -gt 0) {
            $definition.assertions.required_lines += @(
                ("MISSION\.GRAPH: objective insert matched graph source '.*{0}::objectives\.json' index=\d+ node=mission\.objective\.\d+ criteria=\d+" -f $missionArchiveRegex),
                ("MISSION\.GRAPH: objective check routed through graph source '.*{0}::objectives\.json' nodes=\d+ criteria=\d+ backend=graph\.objective\.operands\+graph\.objective\.state\+graph\.objective\.object_state\+graph\.mission\.flags" -f $missionArchiveRegex)
            )
        }
        if ([int]$Entry.MissionObjectiveCriteriaRows -gt 0) {
            $definition.assertions.required_lines +=
                ("MISSION\.GRAPH: objective criteria evaluated from graph source '.*{0}::objectives\.json' index=\d+ criteria=\d+ backend=graph\.objective\.operands\+graph\.objective\.state\+graph\.objective\.object_state\+graph\.mission\.flags" -f $missionArchiveRegex)
        }
        if ([bool]$Entry.MissionHasMissionFlagCriteria) {
            $definition.assertions.required_lines +=
                ("MISSION\.GRAPH: mission flags updated in graph runtime '.*{0}::objectives\.json' flags=0x[0-9a-fA-F]+ backend=graph\.mission\.flags" -f $missionArchiveRegex)
        }
        if ([bool]$Entry.MissionHasObjectStateCriteria) {
            $definition.assertions.required_lines +=
                ("MISSION\.GRAPH: objective object state updated from graph source '.*{0}::objectives\.json' objective=\d+ node=mission\.objective_step\.\d+ tag=-?\d+ present=\d+ healthy=\d+ held=\d+ backend=graph\.objective\.object_state" -f $missionArchiveRegex)
        }
    }

    $path = Join-Path $OutDir "$testName.json"
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $path,
        ($definition | ConvertTo-Json -Depth 12),
        $utf8NoBom)
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
    Write-Host ("  - {0} ({1})" -f $entry.ScenarioCatalogId, $entry.StageNumHex) -ForegroundColor Gray
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

if (-not $KeepGeneratedTests) {
    Remove-Item -LiteralPath $GeneratedTestsDir -Recurse -Force -ErrorAction SilentlyContinue
}

exit $rc
