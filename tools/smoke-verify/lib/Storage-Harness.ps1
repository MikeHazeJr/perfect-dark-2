#Requires -Version 5.1
# Prospective storage only. Historical runs and input templates are read-only.
$script:SmokeStoragePolicy = @{}
$script:SmokeStorageBackend = Join-Path (Split-Path -Parent $PSScriptRoot) 'storage.py'
$script:SmokeStoragePython = ''

function Get-SmokeStoragePython {
    if ($script:SmokeStoragePython) { return $script:SmokeStoragePython }
    # Use the same preferred interpreter as build-headless, without changing PATH.
    # Storage uses Windows paths and native exclusive handles; MSYS Python is
    # unsuitable even when it can launch a script with a translated path.
    $candidates = @('C:/Python312/python.exe')
    $candidates += @(Get-Command python,python3 -All -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Source)
    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf) -or
            $candidate -match '(?i)[\\/]WindowsApps[\\/]') { continue }
        try {
            & $candidate -B -c "import os, sys; sys.exit(0 if os.name == 'nt' else 1)" 2>$null
            if ($LASTEXITCODE -eq 0) {
                $script:SmokeStoragePython = $candidate
                return $script:SmokeStoragePython
            }
        } catch { continue }
    }
    throw 'Smoke storage requires native Windows Python 3. Install it at C:/Python312/python.exe or expose it on PATH; MSYS Python cannot enforce the Windows storage contract.'
}

function Initialize-SmokeStoragePolicy {
    param([string] $Path = '')
    $script:SmokeStoragePolicy = @{}
    if (-not $Path) { $Path = Join-Path (Split-Path -Parent $PSScriptRoot) 'storage-policy.json' }
    $value = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    foreach ($property in $value.PSObject.Properties) { $script:SmokeStoragePolicy[$property.Name] = $property.Value }
}

function Invoke-SmokeStorage {
    param([Parameter(Mandatory)][string] $Action,
          [Parameter(Mandatory)][string] $ProjectRoot, [hashtable] $Request = @{})
    $Request.project = $ProjectRoot
    $Request.policy = $script:SmokeStoragePolicy
    $oldEncoding = $OutputEncoding
    try {
        $OutputEncoding = New-Object System.Text.UTF8Encoding($false)
        $payload = ConvertTo-Json -InputObject $Request -Depth 50 -Compress
        $pythonExe = Get-SmokeStoragePython
        $output = $payload | & $pythonExe -B $script:SmokeStorageBackend $Action
        if ($LASTEXITCODE -ne 0) { throw "Smoke storage $Action refused (exit $LASTEXITCODE); existing evidence preserved." }
        return ($output | ConvertFrom-Json)
    } finally { $OutputEncoding = $oldEncoding }
}

function Assert-SmokeStoragePreflight {
    param([Parameter(Mandatory)][string] $ProjectRoot, [switch] $Build,
          [long] $ArchivePendingBytes = 0)
    $gib = if ($Build) { $script:SmokeStoragePolicy.peak_build_gib } else { $script:SmokeStoragePolicy.peak_run_gib }
    if ($null -eq $gib) { $gib = if ($Build) { 4 } else { 2 } }
    return Invoke-SmokeStorage -Action preflight -ProjectRoot $ProjectRoot -Request @{
        pending_bytes = [long]([double]$gib * 1GB); archive_pending_bytes = $ArchivePendingBytes
    }
}

function New-SmokeManagedInstall {
    param([Parameter(Mandatory)][string] $ProjectRoot,
          [Parameter(Mandatory)][string] $TestName, [string] $InstallState = 'clean',
          [string] $SourceBinary = '', [string] $SourceRom = '', [string] $Target = 'pd',
          [switch] $Shared, [string] $TemplateInstall = '', [string] $SourceSeed = '')
    if ($InstallState -notin @('clean','prefilled','current')) { throw "Unknown install state: $InstallState" }
    $exeName = if ($Target -eq 'pd-server') { 'PerfectDarkServer.exe' } else { 'PerfectDark.exe' }
    $request = @{ name = $TestName; target = $Target; shared = [bool]$Shared; owner_pid = $PID }
    $rom = ''; $romId = ''; $bin = ''
    if ($SourceSeed) {
        if ($TemplateInstall -or $SourceBinary -or $SourceRom) { throw 'SourceSeed cannot be combined with template/binary/ROM inputs.' }
        $request.seed_id = $SourceSeed
        $InstallState = 'current'
    } elseif ($TemplateInstall) {
        $request.template = [System.IO.Path]::GetFullPath($TemplateInstall)
        $bin = Join-Path $request.template $exeName
        if (-not (Test-Path -LiteralPath $bin -PathType Leaf)) { throw "Template binary missing: $bin" }
        $rom = Find-SourceRom -ProjectRoot $request.template
        if ($rom) { $romId = Get-RomIdFromName -RomPath $rom }
        $InstallState = 'current'
    } else {
        $bin = Find-SourceBinary -ProjectRoot $ProjectRoot -ExplicitPath $SourceBinary -Target $Target
        if (-not $bin) { throw "Cannot find $exeName; build first or pass -SourceBinary." }
        $sources = New-Object 'System.Collections.Generic.List[object]'
        $sources.Add(@($bin, $exeName))
        $seen = @{}
        foreach ($directory in @((Split-Path -Parent $bin), (Join-Path $ProjectRoot 'Build'))) {
            if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
            foreach ($dll in @(Get-ChildItem -LiteralPath $directory -Filter '*.dll' -File)) {
                if (-not $seen.ContainsKey($dll.Name)) { $sources.Add(@($dll.FullName, $dll.Name)); $seen[$dll.Name] = $true }
            }
        }
        if ($Target -ne 'pd-server') {
            $rom = Find-SourceRom -ProjectRoot $ProjectRoot -ExplicitPath $SourceRom
            if (-not $rom) { throw 'Cannot find ROM; pass -SourceRom.' }
            $romId = Get-RomIdFromName -RomPath $rom
            $sources.Add(@($rom, [System.IO.Path]::GetFileName($rom)))
            $data = ''
            if ($InstallState -eq 'prefilled') { $data = Join-Path $ProjectRoot ".claude/smoke-verify-cache/$romId" }
            elseif ($InstallState -eq 'current') { $data = Join-Path $ProjectRoot ".claude/smoke-verify-install/data/$romId" }
            if ($data -and (Test-Path -LiteralPath $data)) { $request.data_source = $data }
            elseif ($InstallState -ne 'clean') { Write-Warning 'Read-only seed data unavailable; using clean inputs.' }
        }
        $request.sources = $sources.ToArray(); $request.rom_id = $romId
    }
    $info = Invoke-SmokeStorage -Action seed -ProjectRoot $ProjectRoot -Request $request
    if ($SourceSeed) {
        $bin = Join-Path $info.InstallDir $exeName
        $rom = Find-SourceRom -ProjectRoot $info.InstallDir
        if ($Target -ne 'pd-server' -and -not $rom) { throw 'Immutable seed lacks the client ROM input.' }
        if ($rom) { $romId = Get-RomIdFromName -RomPath $rom }
    }
    foreach ($pair in @(@('SourceBinary',$bin),@('SourceRom',$rom),@('RomId',$romId),
                        @('InstallState',$InstallState),@('Target',$Target),@('ExeName',$exeName))) {
        $info | Add-Member -NotePropertyName $pair[0] -NotePropertyValue $pair[1]
    }
    return $info
}

function Complete-SmokeManagedInstall {
    param([Parameter(Mandatory)][string] $ProjectRoot, [Parameter(Mandatory)][psobject] $InstallInfo,
          [Parameter(Mandatory)][string] $DefinitionPath, [bool] $Passed, [bool] $Keep,
          [hashtable] $Outcome = @{})
    return Invoke-SmokeStorage -Action complete -ProjectRoot $ProjectRoot -Request @{
        id = $InstallInfo.StorageId; owner_pid = $PID; definition = $DefinitionPath
        passed = $Passed; keep = $Keep; outcome = $Outcome
    }
}

function Get-SmokeReusePlan {
    param([Parameter(Mandatory)][string]$ProjectRoot, [Parameter(Mandatory)][string]$InstallId,
          [Parameter(Mandatory)][string]$Seed, [Parameter(Mandatory)][string]$BinarySha256)
    return Invoke-SmokeStorage -Action reuse-plan -ProjectRoot $ProjectRoot -Request @{
        id=$InstallId; seed_id=$Seed; binary_sha256=$BinarySha256
    }
}

function Assert-SmokeReuseRuntimeSupported {
    # Save/home/capture overrides do not isolate extraction sidecars and the
    # asset-directory .pdextract-cache stamp. No current client contract can
    # prevent those writes. Never substitute a caller promise or a hash check
    # after launch for enforcement of immutable inputs.
    throw 'Runtime reuse refused: current client can write extraction sidecars and .pdextract-cache in the base. Supported immutable-base write isolation is required; no game, profile, reset, eviction or copy was started.'
}

function New-SmokeReadOnlyProfile {
    # Storage-only registration for a read-only consumer, not a game launcher.
    param([Parameter(Mandatory)][string]$ProjectRoot, [Parameter(Mandatory)][string]$InstallId,
          [Parameter(Mandatory)][string]$Seed, [Parameter(Mandatory)][string]$BinarySha256)
    return Invoke-SmokeStorage -Action reuse-acquire -ProjectRoot $ProjectRoot -Request @{
        id=$InstallId; seed_id=$Seed; binary_sha256=$BinarySha256; owner_pid=$PID
    }
}

function Complete-SmokeReadOnlyProfile {
    param([Parameter(Mandatory)][string]$ProjectRoot, [Parameter(Mandatory)]$Profile,
          [hashtable]$Outcome=@{})
    return Invoke-SmokeStorage -Action reuse-release -ProjectRoot $ProjectRoot -Request @{
        id=$Profile.ReuseId; owner_pid=$PID; outcome=$Outcome
    }
}

function Assert-SmokePlainTree {
    param([Parameter(Mandatory)][string] $Path, [switch] $Descendants)
    $absolute = [IO.Path]::GetFullPath($Path)
    $cursor = $absolute
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                ($item.PSObject.Properties.Match('LinkType').Count -gt 0 -and $item.LinkType)) {
                throw "Smoke fixture alias rejected: $cursor"
            }
        }
        $next = Split-Path -Parent $cursor
        if ($next -eq $cursor) { break }; $cursor = $next
    }
    if ($Descendants -and (Test-Path -LiteralPath $absolute -PathType Container)) {
        $pending = New-Object 'System.Collections.Generic.Stack[string]'
        $pending.Push($absolute)
        while ($pending.Count) {
            foreach ($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force)) {
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                    ($item.PSObject.Properties.Match('LinkType').Count -gt 0 -and $item.LinkType)) {
                    throw "Smoke fixture alias rejected: $($item.FullName)"
                }
                if ($item.PSIsContainer) { $pending.Push($item.FullName) }
            }
        }
    }
}

function Get-SmokeFixtureDestination {
    param([Parameter(Mandatory)][string] $InstallDir,
          [Parameter(Mandatory)][string] $RelativePath)
    $root = [IO.Path]::GetFullPath($InstallDir).TrimEnd([char[]]'\/')
    $target = [IO.Path]::GetFullPath((Join-Path $root $RelativePath))
    if (-not $target.StartsWith(($root + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Smoke fixture destination escapes private install: $RelativePath"
    }
    Assert-SmokePlainTree -Path $target -Descendants
    return $target
}

Initialize-SmokeStoragePolicy
