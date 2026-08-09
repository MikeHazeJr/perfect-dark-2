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
        $networkLeaf = "network_$label.pdgamemode"
        $networkMember = "gamemodes/$networkLeaf"
        $networkPrefix = "./mods/received/$slotId/$networkMember::"
        $networkId = "ingress:network_$label"
        $networkBytes = New-CatalogIngressTypedAsset -CatalogId $networkId `
            -SourcePrefix $networkPrefix -TargetLength $target
        $pdcaRel = "social/catalog-ingress/$slotId.pdca"
        Write-CatalogIngressPdca -Path (Join-Path $InstallDir $pdcaRel) `
            -Member $networkMember -Bytes $networkBytes
        $receiveLines.Add("$pdcaRel|$slotId|received|0")
    }

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
