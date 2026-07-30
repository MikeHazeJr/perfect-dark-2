<#
.SYNOPSIS
  Install Perfect Dark 2 git hooks and self-test the commit-msg validator.

.DESCRIPTION
  Configures git to use .githooks/ as the hooks directory and verifies the
  commit-msg hook rejects bad messages and accepts well-formed ones.

  Idempotent. Safe to run multiple times. Local config, so one install
  covers every worktree of this repo.

  Bypass during emergencies: git commit --no-verify (only with explicit
  authorization).

.PARAMETER SkipSelfTest
  Configure core.hooksPath without running the rejection / acceptance probe.
  Used by CI or automated callers that just want the configuration step.

.EXAMPLE
  pwsh tools/install-githooks.ps1

.EXAMPLE
  pwsh tools/install-githooks.ps1 -SkipSelfTest
#>

[CmdletBinding()]
param(
  [switch]$SkipSelfTest
)

$ErrorActionPreference = 'Stop'

function Get-RepoRoot {
  $top = (& git rev-parse --show-toplevel) 2>$null
  if ($LASTEXITCODE -ne 0 -or -not $top) {
    throw 'Not inside a git repository (git rev-parse --show-toplevel failed).'
  }
  $candidate = ($top.Trim() -replace '/', '\')
  if (Test-Path $candidate) {
    return $candidate
  }

  $cwd = Get-Location
  while ($cwd -and $cwd.Path -ne $cwd.Root) {
    if ((Test-Path (Join-Path $cwd.Path 'AGENTS.md')) -and
        (Test-Path (Join-Path $cwd.Path 'Tools\Workbench\data\roadmap.json'))) {
      return $cwd.Path
    }
    $cwd = $cwd.Parent
  }
  throw "Could not resolve repo root from git path '$top' or current directory."
}

function Resolve-Python {
  foreach ($candidate in @('python3', 'python', 'py')) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
  }
  throw 'No python interpreter found on PATH (tried python3, python, py).'
}

function Test-CommitMsgRejection {
  param(
    [Parameter(Mandatory)] [string]$RepoRoot,
    [Parameter(Mandatory)] [string]$PythonExe,
    [Parameter(Mandatory)] [string]$BadMessage
  )

  $tmpFile = New-TemporaryFile
  try {
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($tmpFile.FullName, $BadMessage, $utf8NoBom)
    Push-Location $RepoRoot
    try {
      $hookScript = Join-Path $RepoRoot '.githooks\commit-msg.py'
      $previousPreference = $ErrorActionPreference
      $ErrorActionPreference = 'Continue'
      try {
        & $PythonExe $hookScript $tmpFile.FullName *> $null
        $exitCode = $LASTEXITCODE
      } finally {
        $ErrorActionPreference = $previousPreference
      }
      return $exitCode
    } finally {
      Pop-Location
    }
  } finally {
    Remove-Item -Force $tmpFile.FullName -ErrorAction SilentlyContinue
  }
}

function Set-HookExecutableBitBestEffort {
  param(
    [Parameter(Mandatory)] [string]$Path
  )

  $previousPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    & git update-index --chmod=+x -- $Path 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
      Write-Host "Warning: could not mark $Path executable in the git index; continuing." -ForegroundColor Yellow
    }
  } finally {
    $ErrorActionPreference = $previousPreference
  }
}

# Main.

$repoRoot = Get-RepoRoot
Set-Location $repoRoot

$hookDir = Join-Path $repoRoot '.githooks'
$hookSh  = Join-Path $hookDir 'commit-msg'
$hookPy  = Join-Path $hookDir 'commit-msg.py'
$preCommitSh = Join-Path $hookDir 'pre-commit'
$preCommitPy = Join-Path $hookDir 'pre-commit.py'
$assetGuardPy = Join-Path $repoRoot 'tools\asset_native_source_guard.py'

if (-not (Test-Path $hookDir)) {
  throw "Missing .githooks directory at $hookDir. Check out a revision that contains the hook."
}
if (-not (Test-Path $hookSh)) {
  throw "Missing $hookSh."
}
if (-not (Test-Path $hookPy)) {
  throw "Missing $hookPy."
}
if (-not (Test-Path $preCommitSh)) {
  throw "Missing $preCommitSh."
}
if (-not (Test-Path $preCommitPy)) {
  throw "Missing $preCommitPy."
}
if (-not (Test-Path $assetGuardPy)) {
  throw "Missing $assetGuardPy."
}

# Verify python is available, since the hook needs it.
$pythonExe = Resolve-Python
Write-Host "Using python: $pythonExe"

# Configure core.hooksPath idempotently.
$currentPath = (& git config --get core.hooksPath) 2>$null
$desiredPath = '.githooks'
if ($currentPath -ne $desiredPath) {
  Write-Host "Setting git config core.hooksPath = $desiredPath (was: $currentPath)"
  & git config core.hooksPath $desiredPath
  if ($LASTEXITCODE -ne 0) {
    throw 'Failed to set core.hooksPath.'
  }
} else {
  Write-Host 'core.hooksPath already configured.'
}

# Mark the shell entry executable. On Windows the bit is stored in the git
# index but is otherwise a no-op for the filesystem; the hook runs regardless
# under Git for Windows MSYS bash.
Set-HookExecutableBitBestEffort '.githooks/commit-msg'
Set-HookExecutableBitBestEffort '.githooks/commit-msg.py'
Set-HookExecutableBitBestEffort '.githooks/pre-commit'
Set-HookExecutableBitBestEffort '.githooks/pre-commit.py'
Set-HookExecutableBitBestEffort 'tools/asset_native_source_guard.py'

if ($SkipSelfTest) {
  Write-Host 'SkipSelfTest set. Install complete.'
  return
}

Write-Host ''
Write-Host 'Self-test: probing the validator directly (no real commits made).'

Write-Host '  Probing Workbench asset-native-source guard...'
& $pythonExe $assetGuardPy 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) {
  throw "Self-test FAILED: asset native-source guard rejected the repository (exit $LASTEXITCODE)."
}
Write-Host '  [PASS] Workbench asset-native-source guard'

# 1. Bad message should exit non-zero.
$badMsg = 'wip'
$badCode = Test-CommitMsgRejection -RepoRoot $repoRoot -PythonExe $pythonExe -BadMessage $badMsg
if ($badCode -eq 0) {
  throw "Self-test FAILED: validator accepted a bare 'wip' message (exit 0)."
}
Write-Host "  [PASS] rejected 'wip' (exit $badCode)"

# 2. Bad subject format should exit non-zero.
$badFmt = 'chore: random update'
$fmtCode = Test-CommitMsgRejection -RepoRoot $repoRoot -PythonExe $pythonExe -BadMessage $badFmt
if ($fmtCode -eq 0) {
  throw "Self-test FAILED: validator accepted 'chore: random update' (exit 0)."
}
Write-Host "  [PASS] rejected 'chore: random update' (exit $fmtCode)"

# 3. Subject ok but missing Refs trailer should exit non-zero.
$missingRefs = @"
Tooling - T-TOOLING-001: Self-test missing refs trailer

This message has a valid subject and a body paragraph, but no Refs trailer,
so the validator must reject it.
"@
$refsCode = Test-CommitMsgRejection -RepoRoot $repoRoot -PythonExe $pythonExe -BadMessage $missingRefs
if ($refsCode -eq 0) {
  throw 'Self-test FAILED: validator accepted a message with no Refs trailer.'
}
Write-Host "  [PASS] rejected missing-Refs message (exit $refsCode)"

# 4. Well-formed message should exit zero.
$goodMsg = @"
Tooling - T-TOOLING-001: Self-test commit-msg acceptance probe

This is a synthetic message generated by tools/install-githooks.ps1 during
the self-test. It demonstrates the required format: subject prefix, body
paragraph, and Refs trailer.

Refs: T-TOOLING-001
"@
$okCode = Test-CommitMsgRejection -RepoRoot $repoRoot -PythonExe $pythonExe -BadMessage $goodMsg
if ($okCode -ne 0) {
  throw "Self-test FAILED: validator rejected a well-formed message (exit $okCode)."
}
Write-Host "  [PASS] accepted well-formed message (exit 0)"

Write-Host ''
Write-Host 'Self-test passed. commit-msg hook is active for this repo.'
Write-Host 'Format reminder:  <Area> - <WorkbenchItemID>: <summary>'
Write-Host 'Reference:        context/designs/commit-message-standard.md'
