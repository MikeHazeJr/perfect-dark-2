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

function Convert-CatalogIngressArchiveBytes {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $IdPrefix,
        [string] $PrimaryMember = '',
        [string] $PrimaryExtension = '',
        [string] $SourcePrefix = '',
        [string] $PrimarySuffix = '',
        [int] $TargetLength = 0
    )

    Add-Type -AssemblyName System.IO.Compression | Out-Null
    $input = [System.IO.MemoryStream]::new([byte[]]$Bytes)
    $archive = [System.IO.Compression.ZipArchive]::new(
        $input, [System.IO.Compression.ZipArchiveMode]::Read, $false)
    $entries = [System.Collections.Specialized.OrderedDictionary]::new()
    foreach ($entry in $archive.Entries) {
        if (-not $entry.Name) { continue }
        $stream = $entry.Open()
        $memory = [System.IO.MemoryStream]::new()
        $stream.CopyTo($memory)
        $stream.Dispose()
        $entryBytes = $memory.ToArray()
        $memory.Dispose()
        if ([System.IO.Path]::GetExtension($entry.FullName) -match '^\.pd') {
            $entryBytes = Convert-CatalogIngressArchiveBytes -Bytes $entryBytes `
                -IdPrefix $IdPrefix
        } elseif ($entry.FullName -match '\.(ini|json|gltf)$') {
            $text = [System.Text.Encoding]::UTF8.GetString($entryBytes)
            $text = $text.Replace('example:', $IdPrefix)
            $entryBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($text)
        }
        $entries.Add($entry.FullName, $entryBytes)
    }
    $archive.Dispose()
    $input.Dispose()

    if ($PrimaryMember) {
        if (-not $entries.Contains($PrimaryMember)) {
            throw "Catalog ingress template is missing primary member '$PrimaryMember'"
        }
        $memberLength = $TargetLength - $SourcePrefix.Length - $PrimarySuffix.Length
        if ($memberLength -le $PrimaryExtension.Length) {
            throw "Catalog ingress primary member cannot reach target $TargetLength from prefix '$SourcePrefix'"
        }
        $renamed = ('r' * ($memberLength - $PrimaryExtension.Length)) + $PrimaryExtension
        if ($entries.Contains($renamed)) {
            throw "Catalog ingress generated duplicate member '$renamed'"
        }
        $rewritten = [System.Collections.Specialized.OrderedDictionary]::new()
        foreach ($item in $entries.GetEnumerator()) {
            $name = [string]$item.Key
            $entryBytes = [byte[]]$item.Value
            if ($name -match '\.(ini|json|gltf)$') {
                $text = [System.Text.Encoding]::UTF8.GetString($entryBytes)
                $text = $text.Replace($PrimaryMember, $renamed)
                $entryBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($text)
            }
            $rewritten.Add($(if ($name -eq $PrimaryMember) { $renamed } else { $name }),
                $entryBytes)
        }
        $entries = $rewritten
        $actual = $SourcePrefix + $renamed + $PrimarySuffix
        if ($actual.Length -ne $TargetLength) {
            throw "Catalog ingress generated length $($actual.Length), expected $TargetLength"
        }
    }

    return New-CatalogIngressZipBytes -Entries $entries
}

function Get-CatalogIngressFamilySpecs {
    return @(
        @{ Code='an'; Type='animation'; Dir='animations'; Template='animations/character_skeletal.pdanim'; Id='example:character_skeletal'; Primary='animation.gltf'; Ext='.gltf'; Suffix='' },
        @{ Code='ar'; Type='arena'; Dir='arenas'; Template='arenas/tri_arena.pdarena'; Id='example:tri_arena'; Primary='dependencies/assets/scenarios/tri_scenario.pdscenario'; Ext='.pdscenario'; Suffix='' },
        @{ Code='mu'; Type='audio'; Dir='audio/music'; Template='audio/music/tri_song.pdsong'; Id='example:tri_song'; Primary='sequence.mid'; Ext='.mid'; Suffix='' },
        @{ Code='sx'; Type='audio'; Dir='audio/sfx'; Template='audio/sfx/tri_click.pdsfx'; Id='example:tri_click'; Primary='sample.wav'; Ext='.wav'; Suffix='' },
        @{ Code='vo'; Type='audio'; Dir='audio/voice'; Template='audio/voice/tri_voice.pdvoice'; Id='example:tri_voice'; Primary='sample.wav'; Ext='.wav'; Suffix='' },
        @{ Code='bo'; Type='body'; Dir='bodies'; Template='bodies/tri_body.pdbody'; Id='example:tri_body'; Primary='mesh.pdmesh'; Ext='.pdmesh'; Suffix='::model.gltf' },
        @{ Code='bp'; Type='botprofile'; Dir='botprofiles'; Template='botprofiles/tri_botprofile.pdbotprofile'; Id='example:tri_botprofile'; Primary='profile.json'; Ext='.json'; Suffix='' },
        @{ Code='ch'; Type='character'; Dir='characters'; Template='characters/tri_character.pdcharacter'; Id='example:tri_character'; Primary='dependencies/assets/body/tri_body.pdbody'; Ext='.pdbody'; Suffix='' },
        @{ Code='ef'; Type='effect'; Dir='effects'; Template='effects/tri_effect.pdeffect'; Id='example:tri_effect'; Primary='effect.graph.json'; Ext='.graph.json'; Suffix='' },
        @{ Code='en'; Type='entity'; Dir='entities'; Template='entities/tri_entity.pdentity'; Id='example:tri_entity'; Primary='behavior.graph.json'; Ext='.graph.json'; Suffix='' },
        @{ Code='fo'; Type='font'; Dir='fonts'; Template='fonts/tri_font.pdfont'; Id='example:tri_font'; Primary='glyphs.pgm'; Ext='.pgm'; Suffix='' },
        @{ Code='gm'; Type='gamemode'; Dir='gamemodes'; Template='gamemodes/tri_gamemode.pdgamemode'; Id='example:tri_gamemode'; Primary='rules.json'; Ext='.json'; Suffix='' },
        @{ Code='he'; Type='head'; Dir='heads'; Template='heads/tri_head.pdhead'; Id='example:tri_head'; Primary='mesh.pdmesh'; Ext='.pdmesh'; Suffix='::model.gltf' },
        @{ Code='hu'; Type='hud'; Dir='hud'; Template='hud/tri_hud.pdhud'; Id='example:tri_hud'; Primary='layout.json'; Ext='.json'; Suffix='' },
        @{ Code='la'; Type='lang'; Dir='lang'; Template='lang/tri_lang.pdlang'; Id='example:tri_lang'; Primary='strings.json'; Ext='.json'; Suffix='' },
        @{ Code='ma'; Type='material'; Dir='materials'; Template='materials/tri_material.pdmaterial'; Id='example:tri_material'; Primary='material.json'; Ext='.json'; Suffix='' },
        @{ Code='me'; Type='model'; Dir='meshes'; Template='meshes/tri_mesh.pdmesh'; Id='example:tri_mesh'; Primary='model.gltf'; Ext='.gltf'; Suffix='' },
        @{ Code='mi'; Type='mission'; Dir='missions'; Template='missions/tri_mission.pdmission'; Id='example:tri_mission'; Primary='mission.graph.json'; Ext='.graph.json'; Suffix='' },
        @{ Code='pj'; Type='projectile'; Dir='projectiles'; Template='projectiles/tri_projectile.pdprojectile'; Id='example:tri_projectile'; Primary='behavior.graph.json'; Ext='.graph.json'; Suffix='' },
        @{ Code='pr'; Type='prop'; Dir='props'; Template='props/tri_prop.pdprop'; Id='example:tri_prop'; Primary='model.gltf'; Ext='.gltf'; Suffix='' },
        @{ Code='sc'; Type='scenario'; Dir='scenarios'; Template='scenarios/tri_scenario.pdscenario'; Id='example:tri_scenario'; Primary='scene.glb'; Ext='.glb'; Suffix='' },
        @{ Code='sk'; Type='skin'; Dir='skins'; Template='skins/tri_skin.pdskin'; Id='example:tri_skin'; Primary='texture.tga'; Ext='.tga'; Suffix='' },
        @{ Code='tx'; Type='texture'; Dir='textures'; Template='textures/tri_texture.pdtexture'; Id='example:tri_texture'; Primary='texture.png'; Ext='.png'; Suffix='' },
        @{ Code='th'; Type='theme'; Dir='themes'; Template='themes/tri_theme.pdtheme'; Id='example:tri_theme'; Primary='theme.json'; Ext='.json'; Suffix='' },
        @{ Code='ui'; Type='ui'; Dir='ui'; Template='ui/tri_reticle.pdui'; Id='example:tri_reticle'; Primary='texture.png'; Ext='.png'; Suffix='' },
        @{ Code='ve'; Type='vehicle'; Dir='vehicles'; Template='vehicles/tri_vehicle.pdvehicle'; Id='example:tri_vehicle'; Primary='model.gltf'; Ext='.gltf'; Suffix='' },
        @{ Code='we'; Type='weapon'; Dir='weapons'; Template='weapons/tri_weapon.pdweapon'; Id='example:tri_weapon'; Primary='behavior/primary.graph.json'; Ext='.graph.json'; Suffix='' }
    )
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
    $probeLines = [System.Collections.Generic.List[string]]::new()
    $rejectLines = [System.Collections.Generic.List[string]]::new()
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

    # Comprehensive live matrix: all 27 public archive families exercise the
    # old 128-byte boundary, the repository maximum (1023), and atomic 1024
    # rejection through loose typed archives, nested .pdmod members, and the
    # real received-PDCA handler. The checked-in editable examples are the
    # source templates; IDs and only the selected primary member are rewritten.
    $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
    $sourceRoot = Join-Path $repoRoot 'examples\modding\typed-pdxxx-basic'
    $familySpecs = @(Get-CatalogIngressFamilySpecs)
    if ($familySpecs.Count -ne 27) {
        throw "Catalog ingress family inventory drifted: $($familySpecs.Count)"
    }
    $matrixTargets = @(
        @{ Length=128; Label='128'; Code='b' },
        @{ Length=1023; Label='1023'; Code='m' },
        @{ Length=1024; Label='over'; Code='o' }
    )
    $networkMatrix = @{}
    foreach ($target in $matrixTargets) {
        $networkMatrix[$target.Label] =
            [System.Collections.Specialized.OrderedDictionary]::new()
    }

    foreach ($spec in $familySpecs) {
        $templatePath = Join-Path $sourceRoot $spec.Template
        if (-not (Test-Path -LiteralPath $templatePath)) {
            throw "Catalog ingress template is missing: $templatePath"
        }
        $templateBytes = [System.IO.File]::ReadAllBytes($templatePath)
        $archiveExt = [System.IO.Path]::GetExtension($spec.Template)
        foreach ($target in $matrixTargets) {
            $label = $target.Label
            $leaf = "$($spec.Code)_$label$archiveExt"

            $looseMode = 'l'
            $looseIdPrefix = "ingress:$looseMode$($target.Code)$($spec.Code)_"
            $looseId = $spec.Id.Replace('example:', $looseIdPrefix)
            $looseModId = if ($label -eq 'over') {
                'fixture_ingress_loose_over'
            } else { 'fixture_ingress_loose' }
            $looseMember = "$($spec.Dir)/$leaf"
            $loosePrefix = "./mods/$looseModId/$looseMember::"
            $looseBytes = Convert-CatalogIngressArchiveBytes `
                -Bytes $templateBytes -IdPrefix $looseIdPrefix `
                -PrimaryMember $spec.Primary -PrimaryExtension $spec.Ext `
                -SourcePrefix $loosePrefix -PrimarySuffix $spec.Suffix `
                -TargetLength $target.Length
            $looseBase = if ($label -eq 'over') { $looseOverRoot } else { $looseRoot }
            $loosePath = Join-Path $looseBase $looseMember
            New-Item -ItemType Directory -Path (Split-Path -Parent $loosePath) `
                -Force | Out-Null
            [System.IO.File]::WriteAllBytes($loosePath, $looseBytes)
            $probeLines.Add("$($spec.Type)=$looseId")

            $nestedMode = 'n'
            $nestedIdPrefix = "ingress:$nestedMode$($target.Code)$($spec.Code)_"
            $nestedId = $spec.Id.Replace('example:', $nestedIdPrefix)
            $nestedMember = "$($spec.Dir)/$leaf"
            $nestedModLeaf = if ($label -eq 'over') {
                'fixture_ingress_nested_over.pdmod'
            } else { 'fixture_ingress_nested.pdmod' }
            $nestedPrefix = "./mods/$nestedModLeaf::$nestedMember::"
            $nestedBytes = Convert-CatalogIngressArchiveBytes `
                -Bytes $templateBytes -IdPrefix $nestedIdPrefix `
                -PrimaryMember $spec.Primary -PrimaryExtension $spec.Ext `
                -SourcePrefix $nestedPrefix -PrimarySuffix $spec.Suffix `
                -TargetLength $target.Length
            if ($label -eq 'over') {
                $nestedOverEntries[$nestedMember] = $nestedBytes
            } else {
                $nestedEntries[$nestedMember] = $nestedBytes
            }
            $probeLines.Add("$($spec.Type)=$nestedId")

            $networkMode = 'r'
            $slotId = "all_$label"
            $slotSegment = ConvertTo-CatalogIngressStorageSegment -Identity $slotId
            $networkIdPrefix = "ingress:$networkMode$($target.Code)$($spec.Code)_"
            $networkId = $spec.Id.Replace('example:', $networkIdPrefix)
            $networkMember = "$($spec.Dir)/$leaf"
            $networkPrefix = "./mods/$networkCategorySegment/$slotSegment/$networkMember::"
            $networkBytes = Convert-CatalogIngressArchiveBytes `
                -Bytes $templateBytes -IdPrefix $networkIdPrefix `
                -PrimaryMember $spec.Primary -PrimaryExtension $spec.Ext `
                -SourcePrefix $networkPrefix -PrimarySuffix $spec.Suffix `
                -TargetLength $target.Length
            $networkMatrix[$label].Add($networkMember, $networkBytes)
            $probeLines.Add("$($spec.Type)=$networkId")
        }
    }

    foreach ($target in $matrixTargets) {
        $slotId = "all_$($target.Label)"
        $pdcaRel = "social/catalog-ingress/$slotId.pdca"
        Write-CatalogIngressPdcaEntries -Path (Join-Path $InstallDir $pdcaRel) `
            -Entries $networkMatrix[$target.Label]
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
        (Join-Path $networkRoot 'probe-list.txt'), $probeLines, $utf8)
    $rejectLines.Insert(0,
        'archive|./mods/fixture_ingress_nested_over.pdmod|gamemode|ingress:nested_over')
    $rejectLines.Insert(0,
        'folder|./mods/fixture_ingress_loose_over|gamemode|ingress:loose_over')
    [System.IO.File]::WriteAllLines(
        (Join-Path $networkRoot 'reject-list.txt'), $rejectLines, $utf8)
    Write-Host '  generated catalog ingress fixtures (27 families x 3 ingresses x 128/1023/1024 plus 127 representative)' -ForegroundColor DarkGray
}
