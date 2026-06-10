<#
.SYNOPSIS
    CPU-safe sweep of scenario-scene-probe over every .pdscenario in a data tree.

.DESCRIPTION
    Runs the scenario-scene-probe executable (built via
    `build-session.ps1 -Target probe -Session <id>`) against every
    .pdscenario archive's scene source, asserting ok=1 with a nonzero decoded
    image count per archive. This is the keystone B-801 CPU pre-flight: it
    exercises the same scene.glb CPU build path as live renderer activation
    WITHOUT opening a window, touching global renderer state, or uploading any
    GPU resource. It must precede any narrow live renderer pass.

    Exits 0 only if every archive probes ok with images > 0. Any miss, or zero
    archives found, is a non-zero exit. A per-archive image-count snapshot can
    be written so a later regeneration cannot silently drop scene content.

.PARAMETER Session
    Session id whose build output holds scenario-scene-probe.exe
    (.claude/session-builds/<Session>/scenario-scene-probe.exe). Required
    unless -ProbeExe is given.

.PARAMETER ProbeExe
    Explicit path to scenario-scene-probe.exe (overrides -Session lookup).

.PARAMETER DataRoot
    Folder containing the extracted typed-archive tree to sweep. Defaults to
    the fresh verified install tree.

.PARAMETER Member
    Archive member to probe. Defaults to scene.glb with scene.gltf fallback.

.PARAMETER SnapshotOut
    Optional path to write a per-archive image-count JSON snapshot.

.EXAMPLE
    .\devtools\build-session.ps1 -Session probe1 -Target probe
    .\devtools\scenario-scene-probe-sweep.ps1 -Session probe1
#>
[CmdletBinding()]
param(
    [string]$Session,
    [string]$ProbeExe,
    [string]$DataRoot = ".claude/smoke-verify-install/data/ntsc-final",
    [string[]]$Member = @("scene.glb", "scene.gltf"),
    [string]$SnapshotOut
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $ProjectRoot

if (-not $ProbeExe) {
    if (-not $Session) {
        Write-Error "Provide -Session <id> (built with -Target probe) or -ProbeExe <path>."
        exit 2
    }
    $ProbeExe = Join-Path ".claude/session-builds/$Session" "scenario-scene-probe.exe"
}
if (-not (Test-Path -LiteralPath $ProbeExe)) {
    Write-Error "scenario-scene-probe.exe not found at: $ProbeExe`nBuild it first: .\devtools\build-session.ps1 -Session <id> -Target probe"
    exit 2
}

$scenarioDir = Join-Path $DataRoot "scenarios"
if (-not (Test-Path -LiteralPath $scenarioDir)) {
    # Some trees keep scenarios at the data root rather than under scenarios/.
    $scenarioDir = $DataRoot
}

$archives = @(Get-ChildItem -LiteralPath $scenarioDir -Recurse -Filter "*.pdscenario" -File -ErrorAction SilentlyContinue |
    Sort-Object FullName)

if ($archives.Count -eq 0) {
    Write-Error "No .pdscenario archives found under: $scenarioDir"
    exit 1
}

Write-Host "scenario-scene-probe sweep" -ForegroundColor Cyan
Write-Host "  probe:  $ProbeExe"
Write-Host "  data:   $scenarioDir"
Write-Host "  count:  $($archives.Count) .pdscenario archive(s)"
Write-Host ""

$pass = 0
$fail = 0
$failures = @()
$snapshot = @{}

# SCENARIO.RENDER.CPU_PROBE: ok=1 scenario=.. source=.. vertices=N groups=N materials=N images=N ...
$okRe     = [regex]'ok=(\d+)'
$imagesRe = [regex]'images=(\d+)'
$vertsRe  = [regex]'vertices=(\d+)'

foreach ($a in $archives) {
    $name = $a.Name
    $scenarioId = [System.IO.Path]::GetFileNameWithoutExtension($name)
    $ok = $false
    $images = 0
    $verts = 0
    $lastLine = ""
    $usedMember = ""

    foreach ($m in $Member) {
        $scenePath = "$($a.FullName)::$m"
        $out = & $ProbeExe $scenarioId $scenePath 2>&1
        $text = ($out | Out-String)
        $lastLine = ($text -split "`r?`n" | Where-Object { $_ -match 'CPU_PROBE' } | Select-Object -Last 1)
        if (-not $lastLine) { $lastLine = ($text.Trim() -split "`r?`n" | Select-Object -Last 1) }
        $okM = $okRe.Match($text)
        if ($okM.Success -and $okM.Groups[1].Value -eq "1") {
            $ok = $true
            $usedMember = $m
            $im = $imagesRe.Match($text); if ($im.Success) { $images = [int]$im.Groups[1].Value }
            $vm = $vertsRe.Match($text);  if ($vm.Success) { $verts  = [int]$vm.Groups[1].Value }
            break
        }
    }

    if ($ok -and $images -gt 0 -and $verts -gt 0) {
        $pass++
        $snapshot[$name] = @{ ok = 1; images = $images; vertices = $verts; member = $usedMember }
    } else {
        $fail++
        $reason = if (-not $ok) { "ok=0" } elseif ($images -le 0) { "images=0" } else { "vertices=0" }
        $failures += [pscustomobject]@{ Archive = $name; Reason = $reason; Line = $lastLine }
        $snapshot[$name] = @{ ok = [int]$ok; images = $images; vertices = $verts; member = $usedMember }
        Write-Host ("  FAIL {0}  ({1})  {2}" -f $name, $reason, $lastLine) -ForegroundColor Red
    }
}

Write-Host ""
Write-Host ("scenario-scene-probe sweep: pass={0} fail={1} total={2}" -f $pass, $fail, $archives.Count) `
    -ForegroundColor $(if ($fail -eq 0) { "Green" } else { "Red" })

if ($SnapshotOut) {
    $snapshot | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $SnapshotOut -Encoding UTF8
    Write-Host "  snapshot: $SnapshotOut"
}

if ($fail -ne 0) {
    Write-Host ""
    Write-Host "Failed archives:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host ("  - {0}: {1}" -f $_.Archive, $_.Reason) }
    exit 1
}
exit 0
