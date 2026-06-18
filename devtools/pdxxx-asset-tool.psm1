$script:PdxxxKnownTypes = @(
    "pdanim",
    "pdarena",
    "pdbody",
    "pdbotprofile",
    "pdcharacter",
    "pdeffect",
    "pdentity",
    "pdfont",
    "pdgamemode",
    "pdhead",
    "pdhud",
    "pdlang",
    "pdmaterial",
    "pdmesh",
    "pdmission",
    "pdprojectile",
    "pdprop",
    "pdscenario",
    "pdskin",
    "pdsfx",
    "pdsong",
    "pdtexture",
    "pdtheme",
    "pdui",
    "pdvehicle",
    "pdvoice",
    "pdweapon"
)

function Initialize-PdxxxZipSupport {
    try { Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue } catch {}
    try { Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue } catch {}
}

function Get-PdxxxKnownTypes {
    return @("all") + ($script:PdxxxKnownTypes | Sort-Object)
}

function Get-PdxxxDefaultDataRoot {
    param([string]$ProjectRoot)
    if (-not $ProjectRoot) { $ProjectRoot = (Get-Location).Path }
    return [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot "Build\data\ntsc-final"))
}

function Get-PdxxxDefaultExtractRoot {
    param([string]$ProjectRoot)
    if (-not $ProjectRoot) { $ProjectRoot = (Get-Location).Path }
    return [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot ".pdxxx-dev-extracts"))
}

function Test-PdxxxArchiveName {
    param([string]$Name)
    if (-not $Name) { return $false }
    $ext = [System.IO.Path]::GetExtension($Name)
    if (-not $ext) { return $false }
    $type = $ext.TrimStart('.').ToLowerInvariant()
    return $script:PdxxxKnownTypes -contains $type
}

function Get-PdxxxAssetTypeFromName {
    param([string]$Name)
    if (-not $Name) { return "" }
    $ext = [System.IO.Path]::GetExtension($Name)
    if (-not $ext) { return "" }
    return $ext.TrimStart('.').ToLowerInvariant()
}

function ConvertTo-PdxxxSafeName {
    param([string]$Name)
    if (-not $Name) { return "asset" }
    $invalid = [System.IO.Path]::GetInvalidFileNameChars()
    $chars = $Name.ToCharArray()
    for ($i = 0; $i -lt $chars.Length; $i++) {
        if ($invalid -contains $chars[$i]) { $chars[$i] = '_' }
    }
    $safe = -join $chars
    $safe = $safe -replace '\s+', '_'
    if (-not $safe) { return "asset" }
    return $safe
}

function Read-PdxxxZipEntryBytes {
    param(
        [System.IO.Compression.ZipArchive]$Zip,
        [string]$Name,
        [int]$MaxBytes = 262144
    )
    if ($null -eq $Zip -or -not $Name) { return $null }
    $entry = $Zip.GetEntry($Name)
    if ($null -eq $entry -or $entry.Length -gt $MaxBytes) { return $null }
    $stream = $entry.Open()
    try {
        $memory = New-Object System.IO.MemoryStream
        $stream.CopyTo($memory)
        return $memory.ToArray()
    } finally {
        try { $stream.Dispose() } catch {}
    }
}

function Read-PdxxxZipEntryText {
    param(
        [System.IO.Compression.ZipArchive]$Zip,
        [string]$Name,
        [int]$MaxBytes = 262144
    )
    $bytes = Read-PdxxxZipEntryBytes -Zip $Zip -Name $Name -MaxBytes $MaxBytes
    if ($null -eq $bytes) { return $null }
    return [System.Text.Encoding]::UTF8.GetString($bytes)
}

function Get-PdxxxDescriptorId {
    param(
        [System.IO.Compression.ZipArchive]$Zip,
        [string[]]$Names,
        [string]$Type,
        [string]$Fallback
    )
    $candidates = @()
    if ($Type) {
        $kind = $Type.Substring(2)
        $candidates += "$kind.ini"
        if ($Type -eq "pdsong") { $candidates += "music.ini" }
        if ($Type -eq "pdvoice") { $candidates += "voice.ini" }
        if ($Type -eq "pdsfx") { $candidates += "sound.ini" }
    }
    $candidates += ($Names | Where-Object { $_ -match '^[^/\\]+\.ini$' })

    foreach ($name in ($candidates | Select-Object -Unique)) {
        if (-not ($Names -contains $name)) { continue }
        $text = Read-PdxxxZipEntryText -Zip $Zip -Name $name -MaxBytes 65536
        if (-not $text) { continue }
        foreach ($line in ($text -split "`n")) {
            $trim = $line.Trim()
            if ($trim -match '^(catalog_id|id)\s*=\s*(.+)$') {
                $value = $Matches[2].Trim().Trim('"')
                if ($value) { return $value }
            }
        }
    }

    if ($Names -contains "_meta/manifest.json") {
        $manifestText = Read-PdxxxZipEntryText -Zip $Zip -Name "_meta/manifest.json" -MaxBytes 262144
        if ($manifestText) {
            try {
                $manifest = $manifestText | ConvertFrom-Json
                foreach ($field in @("catalog_id", "id", "asset_id", "name")) {
                    if ($manifest.PSObject.Properties.Name -contains $field) {
                        $value = [string]$manifest.$field
                        if ($value) { return $value }
                    }
                }
            } catch {}
        }
    }

    return $Fallback
}

function Get-PdxxxSourceSummary {
    param(
        [string[]]$Names
    )
    $sourceNames = @(
        "model.obj",
        "model.gltf",
        "model.glb",
        "scene.glb",
        "scene.gltf",
        "geometry.obj",
        "animation.gltf",
        "track.wav",
        "track.ogg",
        "track.mp3"
    )
    $sources = @()
    foreach ($name in $sourceNames) {
        if ($Names -contains $name) { $sources += $name }
    }
    $nestedMeshes = @($Names | Where-Object { (Get-PdxxxAssetTypeFromName $_) -eq "pdmesh" })
    if ($nestedMeshes.Count -gt 0) {
        $sources += ("nested pdmesh: " + $nestedMeshes.Count)
    }
    $textures = @($Names | Where-Object { $_ -match '\.(png|tga|jpg|jpeg)$' })
    if ($textures.Count -gt 0) {
        $sources += ("images: " + $textures.Count)
    }
    return ($sources -join ", ")
}

function Get-PdxxxAssetList {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [string]$Type = "all",
        [string]$Search = ""
    )
    Initialize-PdxxxZipSupport
    $items = New-Object System.Collections.ArrayList
    if (-not (Test-Path -LiteralPath $Root)) { return @() }
    $rootFull = [System.IO.Path]::GetFullPath($Root)
    $typeNorm = if ($Type) { $Type.TrimStart('.').ToLowerInvariant() } else { "all" }
    $needle = if ($Search) { $Search.ToLowerInvariant() } else { "" }

    $candidates = @()
    $candidates += Get-ChildItem -LiteralPath $rootFull -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { Test-PdxxxArchiveName $_.Name }
    $candidates += Get-ChildItem -LiteralPath $rootFull -Recurse -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-PdxxxArchiveName $_.Name }

    foreach ($file in ($candidates | Sort-Object FullName)) {
        $assetType = Get-PdxxxAssetTypeFromName $file.Name
        if ($typeNorm -ne "all" -and $assetType -ne $typeNorm) { continue }
        $rel = $file.FullName.Substring($rootFull.Length).TrimStart('\', '/')
        if ($needle -and ($file.Name.ToLowerInvariant().IndexOf($needle) -lt 0) -and ($rel.ToLowerInvariant().IndexOf($needle) -lt 0)) { continue }

        $entryCount = 0
        $meshEntries = 0
        $sourceSummary = ""
        $id = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
        $openable = $false

        if ($file.PSIsContainer) {
            $names = @(Get-ChildItem -LiteralPath $file.FullName -Recurse -File -ErrorAction SilentlyContinue |
                ForEach-Object { $_.FullName.Substring($file.FullName.Length).TrimStart('\', '/') -replace '\\', '/' })
            $entryCount = $names.Count
            $meshEntries = @($names | Where-Object { (Get-PdxxxAssetTypeFromName $_) -eq "pdmesh" }).Count
            $sourceSummary = Get-PdxxxSourceSummary -Names $names
            $openable = $true
        } else {
            try {
                $zip = [System.IO.Compression.ZipFile]::OpenRead($file.FullName)
                try {
                    $names = @($zip.Entries | ForEach-Object { $_.FullName })
                    $entryCount = $names.Count
                    $meshEntries = @($names | Where-Object { (Get-PdxxxAssetTypeFromName $_) -eq "pdmesh" }).Count
                    $sourceSummary = Get-PdxxxSourceSummary -Names $names
                    $id = Get-PdxxxDescriptorId -Zip $zip -Names $names -Type $assetType -Fallback $id
                    $openable = $true
                } finally {
                    try { $zip.Dispose() } catch {}
                }
            } catch {
                $sourceSummary = "not zip-openable"
            }
        }

        [void]$items.Add([PSCustomObject]@{
            Id = $id
            Type = $assetType
            Name = $file.Name
            RelativePath = $rel
            FullPath = $file.FullName
            SizeBytes = if ($file.PSIsContainer) { 0L } else { [int64]$file.Length }
            Entries = $entryCount
            Meshes = $meshEntries
            Sources = $sourceSummary
            Openable = $openable
        })
    }
    return @($items.ToArray())
}

function Test-PdxxxSafeOutputPath {
    param(
        [string]$Root,
        [string]$Path
    )
    try {
        $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
        $pathFull = [System.IO.Path]::GetFullPath($Path)
        return $pathFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)
    } catch {
        return $false
    }
}

function Expand-PdxxxZipArchive {
    param(
        [Parameter(Mandatory=$true)][System.IO.Compression.ZipArchive]$Zip,
        [Parameter(Mandatory=$true)][string]$Destination,
        [int]$Depth = 0,
        [int]$MaxDepth = 4
    )
    if (-not (Test-Path -LiteralPath $Destination)) {
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    }
    $destFull = [System.IO.Path]::GetFullPath($Destination)
    foreach ($entry in $Zip.Entries) {
        $raw = ($entry.FullName -replace '/', '\').TrimStart('\')
        if (-not $raw -or $raw.EndsWith('\')) { continue }
        $target = Join-Path $destFull $raw
        if (-not (Test-PdxxxSafeOutputPath -Root $destFull -Path $target)) {
            throw "Unsafe archive entry path: $($entry.FullName)"
        }

        $entryType = Get-PdxxxAssetTypeFromName $entry.Name
        if ($entryType -and $Depth -lt $MaxDepth) {
            $memory = New-Object System.IO.MemoryStream
            $stream = $entry.Open()
            try { $stream.CopyTo($memory) } finally { try { $stream.Dispose() } catch {} }
            $memory.Position = 0
            $dir = Split-Path -Parent $target
            if ($dir -and -not (Test-Path -LiteralPath $dir)) {
                New-Item -ItemType Directory -Path $dir -Force | Out-Null
            }
            $storedOut = [System.IO.File]::Open($target, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
            try { $memory.CopyTo($storedOut) } finally { try { $storedOut.Dispose() } catch {} }
            $memory.Position = 0
            try {
                $nested = [System.IO.Compression.ZipArchive]::new($memory, [System.IO.Compression.ZipArchiveMode]::Read, $false)
                try {
                    $nestedDest = $target + " extracted"
                    if (-not (Test-Path -LiteralPath $nestedDest)) {
                        New-Item -ItemType Directory -Path $nestedDest -Force | Out-Null
                    }
                    Expand-PdxxxZipArchive -Zip $nested -Destination $nestedDest -Depth ($Depth + 1) -MaxDepth $MaxDepth
                    continue
                } finally {
                    try { $nested.Dispose() } catch {}
                }
            } catch {
                $memory.Position = 0
            }
        }

        $dir = Split-Path -Parent $target
        if ($dir -and -not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        $inStream = $entry.Open()
        $outStream = [System.IO.File]::Open($target, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        try { $inStream.CopyTo($outStream) } finally {
            try { $outStream.Dispose() } catch {}
            try { $inStream.Dispose() } catch {}
        }
    }
}

function Copy-PdxxxDirectoryArchive {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Destination
    )
    if (-not (Test-Path -LiteralPath $Destination)) {
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    }
    Get-ChildItem -LiteralPath $Path -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
        $rel = $_.FullName.Substring($Path.Length).TrimStart('\', '/')
        $target = Join-Path $Destination $rel
        if (-not (Test-PdxxxSafeOutputPath -Root $Destination -Path $target)) {
            throw "Unsafe directory entry path: $rel"
        }
        $dir = Split-Path -Parent $target
        if ($dir -and -not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
}

function Export-PdxxxAssets {
    param(
        [Parameter(Mandatory=$true)][string[]]$Paths,
        [Parameter(Mandatory=$true)][string]$OutputRoot
    )
    Initialize-PdxxxZipSupport
    if (-not (Test-Path -LiteralPath $OutputRoot)) {
        New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    }
    $batch = Join-Path ([System.IO.Path]::GetFullPath($OutputRoot)) (Get-Date -Format "yyyyMMdd-HHmmss")
    New-Item -ItemType Directory -Path $batch -Force | Out-Null
    $results = New-Object System.Collections.ArrayList
    $index = 0
    foreach ($path in $Paths) {
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path
        $safe = ConvertTo-PdxxxSafeName $item.Name
        $dest = Join-Path $batch $safe
        while (Test-Path -LiteralPath $dest) {
            $index++
            $dest = Join-Path $batch ($safe + "_" + $index)
        }
        if ($item.PSIsContainer) {
            Copy-PdxxxDirectoryArchive -Path $item.FullName -Destination $dest
        } else {
            $zip = [System.IO.Compression.ZipFile]::OpenRead($item.FullName)
            try { Expand-PdxxxZipArchive -Zip $zip -Destination $dest } finally { try { $zip.Dispose() } catch {} }
        }
        [void]$results.Add([PSCustomObject]@{
            Source = $item.FullName
            Destination = $dest
        })
    }
    return [PSCustomObject]@{
        Root = $batch
        Count = $results.Count
        Items = $results
    }
}

function Open-PdxxxExtractRoot {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
    Start-Process "explorer.exe" $Path
}

function Invoke-PdxxxAssetToolSelfTest {
    Initialize-PdxxxZipSupport
    $root = Join-Path ([System.IO.Path]::GetTempPath()) ("pdxxx-asset-tool-" + [guid]::NewGuid().ToString("N"))
    $data = Join-Path $root "data\ntsc-final"
    $meshes = Join-Path $data "meshes"
    $bodies = Join-Path $data "bodies"
    $out = Join-Path $root "out"
    New-Item -ItemType Directory -Path $meshes -Force | Out-Null
    New-Item -ItemType Directory -Path $bodies -Force | Out-Null
    try {
        $meshPath = Join-Path $meshes "test_mesh.pdmesh"
        $meshTemp = Join-Path $root "mesh-src"
        New-Item -ItemType Directory -Path $meshTemp -Force | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $meshTemp "mesh.ini"), "id = test:mesh`nmodel_file = model.obj`n", [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText((Join-Path $meshTemp "model.obj"), "v 0 0 0`nv 1 0 0`nv 0 1 0`nf 1 2 3`n", [System.Text.UTF8Encoding]::new($false))
        [System.IO.File]::WriteAllText((Join-Path $meshTemp "model.mtl"), "newmtl tri`n", [System.Text.UTF8Encoding]::new($false))
        [System.IO.Compression.ZipFile]::CreateFromDirectory($meshTemp, $meshPath)

        $bodyPath = Join-Path $bodies "test_body.pdbody"
        $bodyTemp = Join-Path $root "body-src"
        New-Item -ItemType Directory -Path $bodyTemp -Force | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $bodyTemp "body.ini"), "id = test:body`nmesh_archive = mesh.pdmesh`n", [System.Text.UTF8Encoding]::new($false))
        Copy-Item -LiteralPath $meshPath -Destination (Join-Path $bodyTemp "mesh.pdmesh") -Force
        [System.IO.Compression.ZipFile]::CreateFromDirectory($bodyTemp, $bodyPath)

        $list = Get-PdxxxAssetList -Root $data -Type "all"
        if (@($list).Count -ne 2) { throw "selftest expected 2 assets, got $(@($list).Count)" }
        $meshOnly = Get-PdxxxAssetList -Root $data -Type "pdmesh"
        if (@($meshOnly).Count -ne 1) { throw "selftest expected one mesh asset" }
        $result = Export-PdxxxAssets -Paths @($bodyPath) -OutputRoot $out
        $nestedArchive = Join-Path $result.Root "test_body.pdbody\mesh.pdmesh"
        if (-not (Test-Path -LiteralPath $nestedArchive)) { throw "selftest nested archive was not preserved" }
        $nestedObj = Join-Path $result.Root "test_body.pdbody\mesh.pdmesh extracted\model.obj"
        if (-not (Test-Path -LiteralPath $nestedObj)) { throw "selftest nested mesh was not expanded" }
        return "pdxxx asset tool selftest ok"
    } finally {
        try { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue } catch {}
    }
}

Export-ModuleMember -Function `
    Get-PdxxxKnownTypes, `
    Get-PdxxxDefaultDataRoot, `
    Get-PdxxxDefaultExtractRoot, `
    Get-PdxxxAssetList, `
    Export-PdxxxAssets, `
    Open-PdxxxExtractRoot, `
    Invoke-PdxxxAssetToolSelfTest
