param()

function Sync-ProjectMindState {
    param(
        [Parameter(Mandatory=$true)][string]$ProjectRoot,
        [switch]$Quiet
    )

    $result = [ordered]@{
        MemorySource = $null
        MemoryMirror = $null
        MemoryCopied = $false
        MemoryAvailable = $false
        Notes = @()
    }

    $profileRoot = $env:USERPROFILE
    if (-not $profileRoot) {
        $result.Notes += "USERPROFILE is not set; Codex memory mirror skipped."
        return [PSCustomObject]$result
    }

    $memorySource = Join-Path $profileRoot ".codex\memories\MEMORY.md"
    $memoryMirror = Join-Path $ProjectRoot "Tools\Workbench\exports\codex-memory-snapshot.md"
    $result.MemorySource = $memorySource
    $result.MemoryMirror = $memoryMirror

    if (-not (Test-Path -LiteralPath $memorySource)) {
        $result.Notes += "Codex memory source not found; mirror skipped."
        return [PSCustomObject]$result
    }

    $result.MemoryAvailable = $true
    try {
        $mirrorDir = Split-Path -Parent $memoryMirror
        if (-not (Test-Path -LiteralPath $mirrorDir)) {
            New-Item -ItemType Directory -Path $mirrorDir -Force | Out-Null
        }

        $sourceText = [System.IO.File]::ReadAllText($memorySource, [System.Text.Encoding]::UTF8)
        $oldText = $null
        if (Test-Path -LiteralPath $memoryMirror) {
            $oldText = [System.IO.File]::ReadAllText($memoryMirror, [System.Text.Encoding]::UTF8)
        }
        if ($oldText -ne $sourceText) {
            $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
            [System.IO.File]::WriteAllText($memoryMirror, $sourceText, $utf8NoBom)
            $result.MemoryCopied = $true
            if (-not $Quiet) { Write-Host "  [state-sync] mirrored Codex memory to Tools/Workbench/exports/codex-memory-snapshot.md" -ForegroundColor Gray }
        } elseif (-not $Quiet) {
            Write-Host "  [state-sync] Codex memory mirror already current" -ForegroundColor Gray
        }
    } catch {
        $result.Notes += ("Codex memory mirror failed: " + $_.Exception.Message)
    }

    return [PSCustomObject]$result
}

function Get-ProjectStateCommitBody {
    param(
        [Parameter(Mandatory=$true)][string]$ProjectRoot,
        [Parameter(Mandatory=$true)][string]$ActionLabel
    )

    $changed = @()
    try {
        $changed = @(git -C $ProjectRoot diff --cached --name-status 2>$null | ForEach-Object { $_.ToString() })
    } catch {}

    $stateFiles = @(
        "Tools/Workbench/data/roadmap.json",
        "Tools/Workbench/data/notes.jsonl",
        "Tools/Workbench/data/changelog.jsonl",
        "UNRELEASED.md"
    )

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("Dev Window staged pending changes during $ActionLabel so code, Workbench state, Codex memory, and release-note state can move together.")
    $lines.Add("")
    $lines.Add("Live state included:")
    foreach ($file in $stateFiles) {
        if (Test-Path -LiteralPath (Join-Path $ProjectRoot $file)) {
            $lines.Add("- $file")
        }
    }
    if ($changed.Count -gt 0) {
        $lines.Add("")
        $lines.Add("Staged changes:")
        foreach ($entry in ($changed | Select-Object -First 20)) {
            $lines.Add("- $entry")
        }
        if ($changed.Count -gt 20) {
            $lines.Add("- ... and $($changed.Count - 20) more")
        }
    }

    return ($lines -join "`n")
}

function Test-ReleaseNotesLookStale {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $true }
    $text = [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
    if ($text -match 'Base\:\s*v0\.0\.7') { return $true }
    if ($text -match 'Dedicated server occupied player slot 0') { return $true }
    if ($text -match '\*\(none yet\)\*') { return $true }
    return $false
}
