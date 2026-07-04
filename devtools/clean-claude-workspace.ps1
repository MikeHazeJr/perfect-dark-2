<#
.SYNOPSIS
  Reap stale transient files from the .claude session workspace.

.DESCRIPTION
  The .claude/ tree accumulates regenerable session debris: multi-GB smoke
  installs / extraction caches, per-session build dirs, and ad-hoc *.log files
  (some render/extract logs grow past 200 MB). Left alone this bloats the disk
  and -- because the Dev Window's pre-build `git add -A` used to sweep it in --
  once blocked a push by exceeding GitHub's 100 MB file limit.

  Gitignoring stopped the commits; this reaps the files themselves once stale.

  Staleness is by modification time: anything whose newest write is older than
  -MaxAgeHours is removed. Active trees (touched within the window) are kept, so
  this is safe to run at the start of every build.

  SAFETY: operates on an EXPLICIT ALLOWLIST of known-regenerable paths only.
  It never touches config (settings.json, settings.local.json, launch.json,
  skills/), never touches .claude/worktrees/ (may hold uncommitted in-flight
  work), and refuses to delete anything not physically under <root>/.claude/ or
  a <root>/tex_*.png diagnostic image.

.PARAMETER Root
  Repo root. Defaults to the parent of this script's devtools/ directory.

.PARAMETER MaxAgeHours
  Age threshold in hours. Entries whose newest write is older than this are
  reaped. Default 24. Use 0 to reap everything transient regardless of age.

.PARAMETER DryRun
  Report what would be reaped and the total reclaim, but delete nothing.

.PARAMETER Quiet
  Suppress per-entry lines; print only the one-line summary. For build wiring.

.EXAMPLE
  .\devtools\clean-claude-workspace.ps1 -DryRun
  .\devtools\clean-claude-workspace.ps1                 # reap >24h stale
  .\devtools\clean-claude-workspace.ps1 -MaxAgeHours 0  # reap all transient now
#>
[CmdletBinding()]
param(
    [string]$Root,
    [int]$MaxAgeHours = 24,
    [switch]$DryRun,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# --- Resolve + validate the repo root ---------------------------------------
if (-not $Root -or $Root -eq '') {
    $Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
}
try { $Root = (Resolve-Path -LiteralPath $Root).Path } catch {
    Write-Error "clean-claude-workspace: root not found: $Root"; exit 2
}
$claudeDir = Join-Path $Root '.claude'
if (-not (Test-Path -LiteralPath $claudeDir -PathType Container)) {
    if (-not $Quiet) { Write-Host "clean-claude-workspace: no .claude/ under $Root -- nothing to do." }
    exit 0
}
# Canonical prefixes every deletion must fall under (defense in depth).
$claudePrefix = (Resolve-Path -LiteralPath $claudeDir).Path.TrimEnd('\') + '\'
$rootPrefix   = $Root.TrimEnd('\') + '\'

# --- Allowlist: regenerable transient trees under .claude/ ------------------
# Whole-tree units; each is reaped in full when its newest write is stale.
$transientDirs = @(
    'smoke-verify-install'   # full game install(s), multi-GB
    'smoke-verify-cache'     # extraction cache, multi-GB
    'smoke-verify-runs'      # smoke run outputs
    'session-builds'         # per-session isolated build dirs
    'b8-dev-verify'          # build-verify scratch
    'mb2'
    'merge-build'
    'post-merge-build'
    'temp-build'
    'verify-build'
    'git-snapshots'          # devtools/git-snapshot.sh output
    'scratch'                # per-session scratch reports/scripts
    'audit-logs'
    'codex-runs'
    'orchestrator'
    'sprint-reports'
    'tmp'
    'texview'
    'glshot'
)
# NEVER in this list: settings.json, settings.local.json, launch.json, skills,
# worktrees (in-flight work), generated, scheduled_tasks.lock, *.exe.

$cutoffUtc = (Get-Date).ToUniversalTime().AddHours(-1 * [double]$MaxAgeHours)

# --- Helpers ----------------------------------------------------------------

# Newest LastWriteTimeUtc across an item and (for dirs) its descendant files.
# Fast path: if the top-level entry itself is already newer than the cutoff we
# return it immediately without walking the subtree (keeps active trees cheap).
function Get-NewestWriteUtc {
    param([System.IO.FileSystemInfo]$Item, [datetime]$Cutoff)
    $newest = $Item.LastWriteTimeUtc
    if (-not $Item.PSIsContainer) { return $newest }
    if ($newest -ge $Cutoff) { return $newest }   # recent top-level activity -> keep, skip deep scan
    try {
        $m = Get-ChildItem -LiteralPath $Item.FullName -Recurse -File -Force -ErrorAction SilentlyContinue |
             Measure-Object -Property LastWriteTimeUtc -Maximum
        if ($m -and $m.Maximum -and $m.Maximum -gt $newest) { $newest = $m.Maximum }
    } catch {}
    return $newest
}

# Total byte size of an item (recursive for dirs). Best-effort.
function Get-SizeBytes {
    param([System.IO.FileSystemInfo]$Item)
    if (-not $Item.PSIsContainer) { return [int64]$Item.Length }
    try {
        $m = Get-ChildItem -LiteralPath $Item.FullName -Recurse -File -Force -ErrorAction SilentlyContinue |
             Measure-Object -Property Length -Sum
        if ($m -and $m.Sum) { return [int64]$m.Sum }
    } catch {}
    return [int64]0
}

function Format-Size {
    param([int64]$Bytes)
    if ($Bytes -ge 1073741824) { return ('{0:N2} GB' -f ($Bytes / 1073741824)) }
    if ($Bytes -ge 1048576)    { return ('{0:N1} MB' -f ($Bytes / 1048576)) }
    if ($Bytes -ge 1024)       { return ('{0:N1} KB' -f ($Bytes / 1024)) }
    return "$Bytes B"
}

# Build the candidate set: allowlisted dirs + .claude/*.log + root tex_*.png.
$candidates = New-Object System.Collections.ArrayList
foreach ($name in $transientDirs) {
    $p = Join-Path $claudeDir $name
    $it = Get-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue
    if ($it) { [void]$candidates.Add($it) }
}
foreach ($f in (Get-ChildItem -LiteralPath $claudeDir -Filter '*.log' -File -Force -ErrorAction SilentlyContinue)) {
    [void]$candidates.Add($f)
}
foreach ($f in (Get-ChildItem -LiteralPath $Root -Filter 'tex_*.png' -File -Force -ErrorAction SilentlyContinue)) {
    [void]$candidates.Add($f)
}

# --- Reap -------------------------------------------------------------------
$reapedBytes = [int64]0
$reapedCount = 0
$keptCount   = 0

foreach ($it in $candidates) {
    # Defense in depth: only ever delete under .claude/ or a root tex_*.png.
    $full = $it.FullName
    $underClaude = $full.StartsWith($claudePrefix, [System.StringComparison]::OrdinalIgnoreCase)
    $isRootTex   = ($it.PSIsContainer -eq $false) -and `
                   ($full.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) -and `
                   ($it.Name -like 'tex_*.png') -and `
                   ((Split-Path -Parent $full).TrimEnd('\') -eq $Root.TrimEnd('\'))
    if (-not ($underClaude -or $isRootTex)) {
        if (-not $Quiet) { Write-Host ("  ! skip (outside allowed roots): {0}" -f $full) }
        continue
    }

    $newest = Get-NewestWriteUtc -Item $it -Cutoff $cutoffUtc
    if ($newest -ge $cutoffUtc) {
        $keptCount++
        continue   # still active within the window -- keep
    }

    $bytes = Get-SizeBytes -Item $it
    $ageH  = [math]::Round(((Get-Date).ToUniversalTime() - $newest).TotalHours, 1)
    $rel   = $full.Substring($Root.Length).TrimStart('\')

    if ($DryRun) {
        if (-not $Quiet) { Write-Host ("  would reap  {0,10}  {1,6}h  {2}" -f (Format-Size $bytes), $ageH, $rel) }
        $reapedBytes += $bytes; $reapedCount++
        continue
    }

    try {
        Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction Stop
        $reapedBytes += $bytes; $reapedCount++
        if (-not $Quiet) { Write-Host ("  reaped      {0,10}  {1,6}h  {2}" -f (Format-Size $bytes), $ageH, $rel) }
    } catch {
        if (-not $Quiet) { Write-Host ("  ! failed    {0}  ({1})" -f $rel, $_.Exception.Message) }
    }
}

$verb = if ($DryRun) { 'would reap' } else { 'reaped' }
Write-Host ("clean-claude-workspace: {0} {1} stale item(s), {2} reclaimed; {3} kept (< {4}h old)." -f `
    $verb, $reapedCount, (Format-Size $reapedBytes), $keptCount, $MaxAgeHours)
exit 0
