function New-CatalogIngressZipBytes {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [hashtable] $Entries
    )

    Add-Type -AssemblyName System.IO.Compression | Out-Null
    $memory = [System.IO.MemoryStream]::new()
    $zip = [System.IO.Compression.ZipArchive]::new(
        $memory,
        [System.IO.Compression.ZipArchiveMode]::Create,
        $true)
    foreach ($name in $Entries.Keys) {
        $entry = $zip.CreateEntry([string]$name,
            [System.IO.Compression.CompressionLevel]::Optimal)
        $stream = $entry.Open()
        $bytes = $Entries[$name]
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Dispose()
    }
    $zip.Dispose()
    $bytes = $memory.ToArray()
    $memory.Dispose()
    return $bytes
}

function ConvertTo-CatalogIngressStorageSegment {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $Identity
    )

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Identity)
    return -join ($bytes | ForEach-Object { $_.ToString('x2') })
}

function New-CatalogIngressTypedAsset {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $CatalogId,
        [Parameter(Mandatory)] [string] $SourcePrefix,
        [Parameter(Mandatory)] [int] $TargetLength
    )

    $memberLength = $TargetLength - $SourcePrefix.Length
    if ($memberLength -lt 1) {
        throw "Catalog ingress prefix already exceeds target ${TargetLength}: $SourcePrefix"
    }
    $member = 'r' * $memberLength
    $ini = @"
[gamemode]
catalog_id = $CatalogId
name = Catalog Ingress $TargetLength
description = Exact path boundary fixture.
mode_key = combat
min_players = 1
max_players = 8
team_based = 0
requirefeature = 0
rules_file = $member
"@
    $rules = @"
{"schema":"pd2.gamemode.rules.v2","catalog_id":"$CatalogId","mode_key":"combat","name":"Catalog Ingress $TargetLength","description":"Exact path boundary fixture.","players":{"min":1,"max":8},"teams":{"required":false},"requirefeature":0}
"@
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    return New-CatalogIngressZipBytes -Entries @{
        'gamemode.ini' = $utf8.GetBytes($ini)
        $member = $utf8.GetBytes($rules)
    }
}

function Write-CatalogIngressPdca {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [string] $Member,
        [Parameter(Mandatory)] [byte[]] $Bytes
    )

    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $memberBytes = $utf8.GetBytes($Member)
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    $writer = [System.IO.BinaryWriter]::new($stream)
    $writer.Write([uint32]0x41434450)
    $writer.Write([uint16]1)
    $writer.Write([uint16]($memberBytes.Length + 1))
    $writer.Write($memberBytes)
    $writer.Write([byte]0)
    $writer.Write([uint32]$Bytes.Length)
    $writer.Write($Bytes)
    $writer.Dispose()
}

function Write-CatalogIngressPdcaEntries {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [System.Collections.IDictionary] $Entries
    )

    if ($Entries.Count -lt 1 -or $Entries.Count -gt [uint16]::MaxValue) {
        throw "PDCA entry count out of range: $($Entries.Count)"
    }
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    $writer = [System.IO.BinaryWriter]::new($stream)
    $writer.Write([uint32]0x41434450)
    $writer.Write([uint16]$Entries.Count)
    foreach ($item in $Entries.GetEnumerator()) {
        $memberBytes = $utf8.GetBytes([string]$item.Key)
        $bytes = [byte[]]$item.Value
        $writer.Write([uint16]($memberBytes.Length + 1))
        $writer.Write($memberBytes)
        $writer.Write([byte]0)
        $writer.Write([uint32]$bytes.Length)
        $writer.Write($bytes)
    }
    $writer.Dispose()
}

function New-CatalogIngressBoundaryFixtures {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $InstallDir
    )

    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $targets = @(127, 128, 1023, 1024)
    $labels = @('127', '128', '1023', 'over')
    $mods = Join-Path $InstallDir 'mods'
    $looseRoot = Join-Path $mods 'fixture_ingress_loose'
    $looseGames = Join-Path $looseRoot 'gamemodes'
    $looseOverRoot = Join-Path $mods 'fixture_ingress_loose_over'
    $looseOverGames = Join-Path $looseOverRoot 'gamemodes'
    $networkRoot = Join-Path $InstallDir 'social\catalog-ingress'
    New-Item -ItemType Directory -Path $looseGames -Force | Out-Null
    New-Item -ItemType Directory -Path $looseOverGames -Force | Out-Null
    New-Item -ItemType Directory -Path $networkRoot -Force | Out-Null

    [System.IO.File]::WriteAllText((Join-Path $looseRoot 'mod.json'),
        '{"id":"fixture_ingress_loose","name":"Catalog Ingress Loose","version":"1.0.0","author":"PD2 tests"}', $utf8)
    [System.IO.File]::WriteAllText((Join-Path $looseOverRoot 'mod.json'),
        '{"id":"fixture_ingress_loose_over","name":"Catalog Ingress Loose Reject","version":"1.0.0","author":"PD2 tests"}', $utf8)
    [System.IO.File]::WriteAllText((Join-Path $mods '.pdmod-migration-done'),
        'catalog ingress smoke keeps the loose source loose', $utf8)

    $nestedEntries = @{}
    $nestedOverEntries = @{}
    $receiveLines = [System.Collections.Generic.List[string]]::new()
    $networkCategorySegment = ConvertTo-CatalogIngressStorageSegment -Identity 'received'
    for ($i = 0; $i -lt $targets.Count; $i++) {
        $target = $targets[$i]
        $label = $labels[$i]

        $looseLeaf = "loose_$label.pdgamemode"
        $looseModId = if ($label -eq 'over') { 'fixture_ingress_loose_over' } else { 'fixture_ingress_loose' }
        $loosePrefix = "./mods/$looseModId/gamemodes/$looseLeaf::"
        $looseId = "ingress:loose_$label"
        $looseBytes = New-CatalogIngressTypedAsset -CatalogId $looseId `
            -SourcePrefix $loosePrefix -TargetLength $target
        $looseDestination = if ($label -eq 'over') { $looseOverGames } else { $looseGames }
        [System.IO.File]::WriteAllBytes((Join-Path $looseDestination $looseLeaf),
            $looseBytes)

        $nestedLeaf = "nested_$label.pdgamemode"
        $nestedMember = "gamemodes/$nestedLeaf"
        $nestedModLeaf = if ($label -eq 'over') { 'fixture_ingress_nested_over.pdmod' } else { 'fixture_ingress_nested.pdmod' }
        $nestedPrefix = "./mods/$nestedModLeaf::$nestedMember::"
        $nestedId = "ingress:nested_$label"
        $nestedAsset = New-CatalogIngressTypedAsset -CatalogId $nestedId `
            -SourcePrefix $nestedPrefix -TargetLength $target
        if ($label -eq 'over') {
            $nestedOverEntries[$nestedMember] = $nestedAsset
        } else {
            $nestedEntries[$nestedMember] = $nestedAsset
        }

        $slotId = "net_$label"
        $slotSegment = ConvertTo-CatalogIngressStorageSegment -Identity $slotId
        $networkLeaf = "network_$label.pdgamemode"
        $networkMember = "gamemodes/$networkLeaf"
        $networkPrefix = "./mods/$networkCategorySegment/$slotSegment/$networkMember::"
        $networkId = "ingress:network_$label"
        $networkBytes = New-CatalogIngressTypedAsset -CatalogId $networkId `
            -SourcePrefix $networkPrefix -TargetLength $target
        $pdcaRel = "social/catalog-ingress/$slotId.pdca"
        Write-CatalogIngressPdca -Path (Join-Path $InstallDir $pdcaRel) `
            -Member $networkMember -Bytes $networkBytes
        $receiveLines.Add("$pdcaRel|$slotId|received|0")
    }

    # B-1043: one received archive deliberately admits valid loose siblings
    # across every mutable scanner domain, then rejects a typed over-capacity
    # sibling. The installed-client receipt must prove the filesystem and all
    # catalog/provider/dependency/allocator/loader state returned to baseline.
    $mixed = [System.Collections.Specialized.OrderedDictionary]::new()
    $mixedMesh = New-CatalogIngressZipBytes -Entries @{
        'model.obj' = $utf8.GetBytes("o ingress_mixed`n")
    }
    $mixed.Add('weapons/mixed_weapon/weapon.ini', $utf8.GetBytes(@'
[weapon]
catalog_id = ingress:mixed_weapon
name = Mixed Rejected Weapon
'@))
    $mixed.Add('projectiles/mixed_projectile/projectile.ini', $utf8.GetBytes(@'
[projectile]
catalog_id = ingress:mixed_projectile
name = Mixed Rejected Projectile
entity_ref = ingress:mixed_entity
'@))
    $mixed.Add('entities/mixed_entity/entity.ini', $utf8.GetBytes(@'
[entity]
catalog_id = ingress:mixed_entity
name = Mixed Rejected Entity
archetype = object
'@))
    $mixed.Add('textures/mixed_texture/texture.ini', $utf8.GetBytes(@'
[texture]
catalog_id = ingress:mixed_texture
name = Mixed Rejected Texture
width = 1
height = 1
format = rgba32
file_path = texture.bin
'@))
    $mixed.Add('textures/mixed_texture/texture.bin', [byte[]](0, 255, 255, 255))
    $mixed.Add('characters/heads/mixed_head/head.ini', $utf8.GetBytes(@'
[head]
catalog_id = ingress:mixed_head
name = Mixed Rejected Head
mesh_archive = mesh.pdmesh
'@))
    $mixed.Add('characters/heads/mixed_head/mesh.pdmesh', $mixedMesh)
    $mixed.Add('characters/heads/mixed_head/_meta/manifest.json',
        $utf8.GetBytes('{"id":"ingress:mixed_head","ismale":1,"height":180,"scale":1.0,"animscale":1.0}'))
    $mixed.Add('characters/bodies/mixed_body/body.ini', $utf8.GetBytes(@'
[body]
catalog_id = ingress:mixed_body
name = Mixed Rejected Body
mesh_archive = mesh.pdmesh
'@))
    $mixed.Add('characters/bodies/mixed_body/mesh.pdmesh', $mixedMesh)
    $mixed.Add('characters/bodies/mixed_body/_meta/manifest.json',
        $utf8.GetBytes('{"id":"ingress:mixed_body","ismale":1,"height":180,"scale":1.0,"animscale":1.0}'))
    $mixed.Add('maps/mixed_arena/arena.ini', $utf8.GetBytes(@'
[arena]
catalog_id = ingress:mixed_arena
name = Mixed Rejected Arena
load_mode = 0
'@))
    $mixed.Add('gamemodes/mixed_good/gamemode.ini', $utf8.GetBytes(@'
[gamemode]
catalog_id = ingress:mixed_good
name = Mixed Rejected Gamemode
mode_key = combat
min_players = 1
max_players = 8
team_based = 0
rules_file = rules.json
'@))
    $mixed.Add('gamemodes/mixed_good/rules.json', $utf8.GetBytes(
        '{"schema":"pd2.gamemode.rules.v2","catalog_id":"ingress:mixed_good","mode_key":"combat","name":"Mixed Rejected Gamemode","players":{"min":1,"max":8},"teams":{"required":false},"requirefeature":0}'))
    $mixed.Add('audio/sfx/mixed_sound/sound.ini', $utf8.GetBytes(@'
[sound]
catalog_id = ingress:mixed_sound
name = Mixed Rejected Sound
audio_category = sfx
file_path = sound.wav
'@))
    $mixed.Add('audio/sfx/mixed_sound/sound.wav',
        $utf8.GetBytes('RIFF_PD2_MIXED_REJECT'))
    $mixed.Add('animations/weapon/mixed_anim/animation.ini', $utf8.GetBytes(@'
[animation]
catalog_id = ingress:mixed_anim
name = Mixed Rejected Animation
category = weapon_animation
commands_file = commands.json
'@))
    $mixed.Add('animations/weapon/mixed_anim/commands.json', $utf8.GetBytes(
        '{"id":"ingress:mixed_anim","commands":[{"command":"end"}]}'))
    $mixedSlotSegment = ConvertTo-CatalogIngressStorageSegment -Identity 'net_mixed'
    $mixedBadPrefix = "./mods/$networkCategorySegment/$mixedSlotSegment/gamemodes/mixed_bad.pdgamemode::"
    $mixed.Add('gamemodes/mixed_bad.pdgamemode',
        (New-CatalogIngressTypedAsset -CatalogId 'ingress:mixed_bad' `
            -SourcePrefix $mixedBadPrefix -TargetLength 1024))
    $mixedPdcaRel = 'social/catalog-ingress/net_mixed.pdca'
    Write-CatalogIngressPdcaEntries -Path (Join-Path $InstallDir $mixedPdcaRel) `
        -Entries $mixed
    $receiveLines.Add("$mixedPdcaRel|net_mixed|received|0")

    $nestedEntries['mod.json'] = $utf8.GetBytes(
        '{"id":"fixture_ingress_nested","name":"Catalog Ingress Nested","version":"1.0.0","author":"PD2 tests"}')
    $nestedBytes = New-CatalogIngressZipBytes -Entries $nestedEntries
    [System.IO.File]::WriteAllBytes(
        (Join-Path $mods 'fixture_ingress_nested.pdmod'), $nestedBytes)
    $nestedOverEntries['mod.json'] = $utf8.GetBytes(
        '{"id":"fixture_ingress_nested_over","name":"Catalog Ingress Nested Reject","version":"1.0.0","author":"PD2 tests"}')
    $nestedOverBytes = New-CatalogIngressZipBytes -Entries $nestedOverEntries
    [System.IO.File]::WriteAllBytes(
        (Join-Path $mods 'fixture_ingress_nested_over.pdmod'), $nestedOverBytes)

    [System.IO.File]::WriteAllText((Join-Path $InstallDir 'mods-enabled.json'),
        '["fixture_ingress_loose","fixture_ingress_nested"]', $utf8)
    [System.IO.File]::WriteAllLines(
        (Join-Path $networkRoot 'receive-list.txt'), $receiveLines, $utf8)
    [System.IO.File]::WriteAllLines(
        (Join-Path $networkRoot 'reject-list.txt'), @(
            'folder|./mods/fixture_ingress_loose_over|ingress:loose_over',
            'archive|./mods/fixture_ingress_nested_over.pdmod|ingress:nested_over'
        ), $utf8)
    Write-Host '  generated catalog ingress boundary fixtures (3 ingresses x 4 boundaries)' -ForegroundColor DarkGray
}
