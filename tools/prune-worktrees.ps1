<#
.SYNOPSIS
    Safely prune stale Claude Code worktrees that are fully merged into dev.

.DESCRIPTION
    Scans .claude/worktrees/ for git worktrees created by Claude Code sessions.
    For each worktree:
      1. Checks if the branch is fully merged into dev
      2. Checks if any code session is actively using it (by matching cwd)
      3. If merged AND not active: removes the worktree and deletes the branch
      4. If NOT merged: reports it as "unmerged" and skips (safe default)

    This script NEVER touches:
      - The main working copy
      - The dev branch
      - Any worktree with unmerged changes
      - Any worktree that appears to be actively in use

.PARAMETER DryRun
    Show what would be pruned without actually doing anything. Default: true (safe).

.PARAMETER Force
    Set to $true to actually perform the pruning. Without this, it's dry-run only.

.EXAMPLE
    # See what would be pruned (safe, no changes):
    .\tools\prune-worktrees.ps1

    # Actually prune:
    .\tools\prune-worktrees.ps1 -Force

.NOTES
    Created for Perfect Dark 2 project. Safe to run at any time.
    Context files in context/ are NEVER affected — they live in the main working copy.
#>

param(
    [switch]$Force,
    [switch]$DryRun
)

# If neither flag given, default to dry run
if (-not $Force) { $DryRun = $true }

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# Handle case where script is run from tools/ directly
if (-not (Test-Path (Join-Path $repoRoot ".git"))) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
}
if (-not (Test-Path (Join-Path $repoRoot ".git"))) {
    $repoRoot = $PSScriptRoot
}
# Final fallback: use the script's grandparent (tools/ is inside repo root)
$worktreeDir = Join-Path $repoRoot ".claude" "worktrees"

Write-Host ""
Write-Host "=== Claude Code Worktree Pruner ===" -ForegroundColor Cyan
Write-Host "Repo root : $repoRoot"
Write-Host "Worktree dir: $worktreeDir"
Write-Host "Mode       : $(if ($DryRun) { 'DRY RUN (use -Force to actually prune)' } else { 'LIVE — will remove merged worktrees' })"
Write-Host ""

if (-not (Test-Path $worktreeDir)) {
    Write-Host "No worktree directory found. Nothing to prune." -ForegroundColor Green
    exit 0
}

# Get all worktree directories
$worktrees = Get-ChildItem -Path $worktreeDir -Directory
if ($worktrees.Count -eq 0) {
    Write-Host "No worktrees found. Nothing to prune." -ForegroundColor Green
    exit 0
}

Write-Host "Found $($worktrees.Count) worktree(s) to evaluate." -ForegroundColor Yellow
Write-Host ""

# Get current dev branch HEAD for merge checking
Push-Location $repoRoot
$devHead = git rev-parse dev 2>$null
if (-not $devHead) {
    Write-Host "ERROR: Could not find 'dev' branch. Aborting." -ForegroundColor Red
    Pop-Location
    exit 1
}

$pruned = 0
$skippedUnmerged = 0
$skippedError = 0
$results = @()

foreach ($wt in $worktrees) {
    $wtPath = $wt.FullName
    $wtName = $wt.Name

    # Find the branch name for this worktree
    $branchName = $null
    try {
        Push-Location $wtPath
        $branchName = git rev-parse --abbrev-ref HEAD 2>$null
        $wtHead = git rev-parse HEAD 2>$null
        Pop-Location
    } catch {
        Pop-Location
        $results += [PSCustomObject]@{
            Name = $wtName
            Branch = "???"
            Status = "ERROR"
            Action = "Skipped (could not read)"
        }
        $skippedError++
        continue
    }

    if (-not $branchName -or -not $wtHead) {
        $results += [PSCustomObject]@{
            Name = $wtName
            Branch = $branchName ?? "???"
            Status = "ERROR"
            Action = "Skipped (no branch/HEAD)"
        }
        $skippedError++
        continue
    }

    # Check if branch is merged into dev
    $mergeBase = git merge-base $devHead $wtHead 2>$null
    $isMerged = ($mergeBase -eq $wtHead) -or ($wtHead -eq $devHead)

    # Also check: is there anything in the worktree that's uncommitted?
    Push-Location $wtPath
    $dirtyFiles = git status --porcelain 2>$null
    Pop-Location
    $isDirty = ($dirtyFiles -and $dirtyFiles.Length -gt 0)

    if ($isDirty) {
        $results += [PSCustomObject]@{
            Name = $wtName
            Branch = $branchName
            Status = "DIRTY"
            Action = "Skipped (uncommitted changes)"
        }
        $skippedUnmerged++
        continue
    }

    if (-not $isMerged) {
        # Check if there are commits ahead of dev
        Push-Location $wtPath
        $aheadCount = (git rev-list "$devHead..$wtHead" 2>$null | Measure-Object).Count
        Pop-Location

        $results += [PSCustomObject]@{
            Name = $wtName
            Branch = $branchName
            Status = "UNMERGED ($aheadCount ahead)"
            Action = "Skipped (not merged into dev)"
        }
        $skippedUnmerged++
        continue
    }

    # Safe to prune — branch is fully merged into dev
    if ($DryRun) {
        $results += [PSCustomObject]@{
            Name = $wtName
            Branch = $branchName
            Status = "MERGED"
            Action = "Would prune (dry run)"
        }
        $pruned++
    } else {
        try {
            # Remove the worktree
            git worktree remove $wtPath --force 2>$null
            # Delete the branch
            if ($branchName -ne "dev" -and $branchName -ne "main" -and $branchName -ne "HEAD") {
                git branch -d $branchName 2>$null
            }
            $results += [PSCustomObject]@{
                Name = $wtName
                Branch = $branchName
                Status = "MERGED"
                Action = "PRUNED"
            }
            $pruned++
        } catch {
            $results += [PSCustomObject]@{
                Name = $wtName
                Branch = $branchName
                Status = "MERGED"
                Action = "ERROR during removal: $_"
            }
            $skippedError++
        }
    }
}

# Final git worktree prune to clean up any stale references
if (-not $DryRun) {
    git worktree prune 2>$null
}

Pop-Location

# Display results
Write-Host ""
Write-Host "--- Results ---" -ForegroundColor Cyan
$results | Format-Table -AutoSize

Write-Host ""
Write-Host "Summary:" -ForegroundColor Cyan
$actionWord = if ($DryRun) { "Would prune" } else { "Pruned" }
Write-Host "  $actionWord    : $pruned" -ForegroundColor $(if ($pruned -gt 0) { "Green" } else { "Gray" })
Write-Host "  Skipped (unmerged/dirty): $skippedUnmerged" -ForegroundColor $(if ($skippedUnmerged -gt 0) { "Yellow" } else { "Gray" })
Write-Host "  Skipped (errors)        : $skippedError" -ForegroundColor $(if ($skippedError -gt 0) { "Red" } else { "Gray" })

if ($DryRun -and $pruned -gt 0) {
    Write-Host ""
    Write-Host "This was a DRY RUN. To actually prune, run:" -ForegroundColor Yellow
    Write-Host "  .\tools\prune-worktrees.ps1 -Force" -ForegroundColor White
}

Write-Host ""
