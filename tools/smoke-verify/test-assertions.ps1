#Requires -Version 5.1
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir "lib\Test-Assertions.ps1")

function Assert-True {
    param(
        [Parameter(Mandatory)] [bool] $Condition,
        [Parameter(Mandatory)] [string] $Message
    )
    if (-not $Condition) {
        throw "assertion self-test failed: $Message"
    }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "pd2-smoke-assertions-{0}" -f [Guid]::NewGuid().ToString("N"))
$logPath = Join-Path $tempRoot "ordered.log"

try {
    [System.IO.Directory]::CreateDirectory($tempRoot) | Out-Null
    [System.IO.File]::WriteAllLines($logPath, @(
        "phase=ready",
        "phase=focus-lost",
        "phase=owned-press",
        "phase=gameplay-read",
        "phase=effect"
    ))

    $ordered = [PSCustomObject]@{
        required_sequences = @(
            [PSCustomObject]@{
                name = "causal-success"
                patterns = @("focus-lost", "owned-press", "gameplay-read", "effect")
            }
        )
    }
    $orderedResult = Invoke-SmokeAssertions -LogPath $logPath -Assertions $ordered
    Assert-True $orderedResult.Passed "ordered sequence should pass"
    Assert-True ($orderedResult.Total -eq 1 -and $orderedResult.Met -eq 1) `
        "one sequence must count as one assertion"

    $anchoredResult = Invoke-SmokeAssertions -LogPath $logPath `
        -Assertions $ordered -RequiredSequenceStartLine 1
    Assert-True $anchoredResult.Passed `
        "sequence should pass when anchored to its exact first witness line"

    $beforeWitnessResult = Invoke-SmokeAssertions -LogPath $logPath `
        -Assertions $ordered -RequiredSequenceStartLine 0
    Assert-True (-not $beforeWitnessResult.Passed) `
        "anchored first pattern must not search forward from an earlier line"

    $pastWitnessResult = Invoke-SmokeAssertions -LogPath $logPath `
        -Assertions $ordered -RequiredSequenceStartLine 2
    Assert-True (-not $pastWitnessResult.Passed) `
        "sequence anchor must ignore a complete sequence that began earlier"
    Assert-True ($pastWitnessResult.Failures[0].Kind -eq "required_sequence_mismatch") `
        "past-witness anchor must report required_sequence_mismatch"

    $mixedAnchors = [PSCustomObject]@{
        required_sequences = @(
            [PSCustomObject]@{
                name = "pre-focus-whole-log"
                patterns = @("ready", "focus-lost")
            },
            [PSCustomObject]@{
                name = "post-focus-owned-read"
                anchor = "window_focus_transition"
                patterns = @("focus-lost", "owned-press", "gameplay-read")
            }
        )
    }
    $namedAnchorLines = @{ window_focus_transition = 1 }
    $mixedAnchorResult = Invoke-SmokeAssertions -LogPath $logPath `
        -Assertions $mixedAnchors -RequiredSequenceStartLines $namedAnchorLines
    Assert-True $mixedAnchorResult.Passed `
        "named anchor should scope only its tagged sequence"
    Assert-True ($mixedAnchorResult.Total -eq 2 -and $mixedAnchorResult.Met -eq 2) `
        "tagged and untagged sequences should retain independent scopes"

    $lateNamedAnchorLines = @{ window_focus_transition = 2 }
    $lateNamedAnchorResult = Invoke-SmokeAssertions -LogPath $logPath `
        -Assertions $mixedAnchors -RequiredSequenceStartLines $lateNamedAnchorLines
    Assert-True (-not $lateNamedAnchorResult.Passed) `
        "named anchor should require its tagged first pattern on the exact line"
    Assert-True ($lateNamedAnchorResult.Total -eq 2 -and $lateNamedAnchorResult.Met -eq 1) `
        "an invalid tagged anchor must not invalidate an unrelated whole-log sequence"
    Assert-True ($lateNamedAnchorResult.Failures[0].Name -eq "post-focus-owned-read") `
        "named anchor mismatch must identify only the tagged sequence"

    $missingNamedAnchorResult = Invoke-SmokeAssertions -LogPath $logPath `
        -Assertions $mixedAnchors -RequiredSequenceStartLines @{}
    Assert-True (-not $missingNamedAnchorResult.Passed) `
        "missing named anchor must fail closed"
    Assert-True ($missingNamedAnchorResult.Failures[0].Kind -eq "required_sequence_anchor_missing") `
        "missing named anchor must report required_sequence_anchor_missing"

    $sentinelAnchorLines = @{ window_focus_transition = [int]::MaxValue }
    $sentinelAnchorResult = Invoke-SmokeAssertions -LogPath $logPath `
        -Assertions $mixedAnchors -RequiredSequenceStartLines $sentinelAnchorLines
    Assert-True (-not $sentinelAnchorResult.Passed) `
        "unreachable named anchor must reject stale causal evidence"
    Assert-True ($sentinelAnchorResult.Met -eq 1) `
        "unreachable named anchor must preserve unrelated whole-log evidence"

    $reversed = [PSCustomObject]@{
        required_sequences = @(
            [PSCustomObject]@{
                name = "causal-reversal"
                patterns = @("gameplay-read", "owned-press")
            }
        )
    }
    $reversedResult = Invoke-SmokeAssertions -LogPath $logPath -Assertions $reversed
    Assert-True (-not $reversedResult.Passed) "reversed sequence must fail"
    Assert-True ($reversedResult.Failures[0].Kind -eq "required_sequence_mismatch") `
        "reversed sequence must report required_sequence_mismatch"

    $singleOccurrence = [PSCustomObject]@{
        required_sequences = @(
            [PSCustomObject]@{
                name = "strict-later-line"
                patterns = @("owned-press", "owned-press")
            }
        )
    }
    $singleResult = Invoke-SmokeAssertions -LogPath $logPath -Assertions $singleOccurrence
    Assert-True (-not $singleResult.Passed) `
        "one line must not satisfy two ordered steps"
    Assert-True ($singleResult.Failures[0].Matched -eq 1) `
        "duplicate-pattern failure must retain the first match"

    $malformed = [PSCustomObject]@{
        required_sequences = @(
            [PSCustomObject]@{ name = ""; patterns = @("ready") }
        )
    }
    $malformedResult = Invoke-SmokeAssertions -LogPath $logPath -Assertions $malformed
    Assert-True (-not $malformedResult.Passed) "malformed sequence must fail closed"
    Assert-True ($malformedResult.Failures[0].Kind -eq "required_sequence_invalid") `
        "malformed sequence must report required_sequence_invalid"

    Write-Host "PASS: smoke assertion ordered-sequence and scoped-anchor self-test"
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
