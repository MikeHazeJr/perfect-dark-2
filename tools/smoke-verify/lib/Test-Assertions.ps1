#Requires -Version 5.1
<#
.SYNOPSIS
    Assertion engine for the Phase 1 smoke verify gate.

.DESCRIPTION
    Pure log-text inspection. The harness in the PD client writes
    structured log lines; this module re-checks the test definition's
    declared assertions against the resulting file.

    Four assertion families:

      required_lines   -- each regex must match at least one log line.
      required_sequences -- each named regex list must match in strict
                            line order (one assertion per sequence); an
                            optional anchor selects a runner-supplied exact
                            first-witness line for only that sequence.
      forbidden_patterns -- no log line may match any pattern.
      required_counts  -- pattern must match between min and max times
                          (max < 0 means no upper bound).

    Designed to be dot-sourced by run.ps1; no parameters or side effects
    at load time.

    c115 (2026-05-14): the assertion verbose switch is named
    $VerboseAssertions, NOT $Verbose. PowerShell auto-binds the common
    -Verbose parameter on every [CmdletBinding()] function, so a custom
    [switch] $Verbose collides at the call site -- under pwsh 7 you get
    a clear binding error; under PowerShell 5.1 it surfaces as the
    misleading "Count not found" strict-mode crash. Keep the name as
    $VerboseAssertions; do not "fix" it back to $Verbose.
#>

Set-StrictMode -Version Latest

function Invoke-SmokeAssertions {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LogPath,
        [Parameter(Mandatory)] [psobject] $Assertions,
        [ValidateRange(-1, 2147483647)] [int] $RequiredSequenceStartLine = -1,
        [System.Collections.IDictionary] $RequiredSequenceStartLines = @{},
        [switch] $VerboseAssertions
    )

    $result = [PSCustomObject]@{
        Passed = $true
        Total = 0
        Met = 0
        Failures = New-Object System.Collections.Generic.List[psobject]
    }

    if (-not (Test-Path -LiteralPath $LogPath)) {
        $result.Passed = $false
        $result.Failures.Add([PSCustomObject]@{
            Kind = "log_missing"
            Message = "log file not found: $LogPath"
        })
        return $result
    }

    $lines = @(Get-Content -LiteralPath $LogPath -ErrorAction SilentlyContinue)

    # Forbidden first: a single match here is sufficient to fail the test.
    $forbidden = @()
    if ($Assertions.PSObject.Properties.Match('forbidden_patterns').Count -gt 0) {
        $forbidden = @($Assertions.forbidden_patterns)
    }

    foreach ($pat in $forbidden) {
        if ([string]::IsNullOrWhiteSpace($pat)) { continue }
        $result.Total++
        $hit = $false
        foreach ($line in $lines) {
            if ($line -match $pat) {
                $result.Failures.Add([PSCustomObject]@{
                    Kind = "forbidden_pattern_matched"
                    Pattern = $pat
                    Line = $line
                })
                $hit = $true
                break
            }
        }
        if (-not $hit) {
            $result.Met++
            if ($VerboseAssertions) { Write-Host "  ok  forbidden absent: $pat" -ForegroundColor DarkGreen }
        } else {
            if ($VerboseAssertions) { Write-Host "  FAIL forbidden matched: $pat" -ForegroundColor Red }
        }
    }

    # Required lines
    $required = @()
    if ($Assertions.PSObject.Properties.Match('required_lines').Count -gt 0) {
        $required = @($Assertions.required_lines)
    }
    foreach ($pat in $required) {
        if ([string]::IsNullOrWhiteSpace($pat)) { continue }
        $result.Total++
        $hit = $false
        foreach ($line in $lines) {
            if ($line -match $pat) { $hit = $true; break }
        }
        if ($hit) {
            $result.Met++
            if ($VerboseAssertions) { Write-Host "  ok  required matched: $pat" -ForegroundColor DarkGreen }
        } else {
            $result.Failures.Add([PSCustomObject]@{
                Kind = "required_line_missing"
                Pattern = $pat
            })
            if ($VerboseAssertions) { Write-Host "  FAIL required missing: $pat" -ForegroundColor Red }
        }
    }

    # Required ordered sequences. Each sequence is one assertion so adding
    # more causal detail does not inflate a receipt with one count per step.
    # A subsequent pattern must match a strictly later line; one log line
    # cannot satisfy two steps, even when the regex text is identical.
    $sequences = @()
    if ($Assertions.PSObject.Properties.Match('required_sequences').Count -gt 0) {
        $sequences = @($Assertions.required_sequences)
    }
    foreach ($sequence in $sequences) {
        $result.Total++

        $name = ""
        $patterns = @()
        $anchorName = ""
        if ($null -ne $sequence -and
                $sequence.PSObject.Properties.Match('name').Count -gt 0) {
            $name = [string]$sequence.name
        }
        if ($null -ne $sequence -and
                $sequence.PSObject.Properties.Match('patterns').Count -gt 0) {
            $patterns = @($sequence.patterns)
        }
        if ($null -ne $sequence -and
                $sequence.PSObject.Properties.Match('anchor').Count -gt 0) {
            $anchorName = [string]$sequence.anchor
        }

        $invalidReason = ""
        if ([string]::IsNullOrWhiteSpace($name)) {
            $invalidReason = "name must be a non-empty string"
        } elseif ($sequence.PSObject.Properties.Match('anchor').Count -gt 0 -and
                [string]::IsNullOrWhiteSpace($anchorName)) {
            $invalidReason = "anchor must be a non-empty string when present"
        } elseif ($patterns.Count -eq 0) {
            $invalidReason = "patterns must contain at least one regex"
        } else {
            for ($patternIndex = 0; $patternIndex -lt $patterns.Count; $patternIndex++) {
                if ([string]::IsNullOrWhiteSpace([string]$patterns[$patternIndex])) {
                    $invalidReason = "pattern step $($patternIndex + 1) must be non-empty"
                    break
                }
            }
        }

        if ($invalidReason) {
            $result.Failures.Add([PSCustomObject]@{
                Kind = "required_sequence_invalid"
                Name = if ($name) { $name } else { "<unnamed>" }
                Message = $invalidReason
            })
            if ($VerboseAssertions) {
                Write-Host "  FAIL sequence invalid: $name ($invalidReason)" -ForegroundColor Red
            }
            continue
        }

        # Required lines/counts/forbidden patterns intentionally inspect the
        # complete process log. A named sequence anchor overrides the legacy
        # global anchor for only that sequence. Its first pattern must match the
        # exact runner-observed witness line; only later steps may search
        # forward. Untagged sequences remain whole-log unless a legacy caller
        # explicitly supplies -RequiredSequenceStartLine.
        $sequenceStartLine = $RequiredSequenceStartLine
        if ($anchorName) {
            if ($null -eq $RequiredSequenceStartLines -or
                    -not $RequiredSequenceStartLines.Contains($anchorName)) {
                $result.Failures.Add([PSCustomObject]@{
                    Kind = "required_sequence_anchor_missing"
                    Name = $name
                    Anchor = $anchorName
                })
                if ($VerboseAssertions) {
                    Write-Host "  FAIL sequence anchor missing: $name ($anchorName)" -ForegroundColor Red
                }
                continue
            }

            $parsedAnchor = -1
            if (-not [int]::TryParse(
                    [string]$RequiredSequenceStartLines[$anchorName],
                    [ref]$parsedAnchor) -or $parsedAnchor -lt 0) {
                $result.Failures.Add([PSCustomObject]@{
                    Kind = "required_sequence_invalid"
                    Name = $name
                    Message = "anchor '$anchorName' must resolve to a non-negative line"
                })
                if ($VerboseAssertions) {
                    Write-Host "  FAIL sequence anchor invalid: $name ($anchorName)" -ForegroundColor Red
                }
                continue
            }
            $sequenceStartLine = $parsedAnchor
        }

        $anchored = $sequenceStartLine -ge 0
        $cursor = if ($anchored) { $sequenceStartLine } else { 0 }
        $matched = 0
        $lastLine = 0
        $failed = $false
        for ($patternIndex = 0; $patternIndex -lt $patterns.Count; $patternIndex++) {
            $pat = [string]$patterns[$patternIndex]
            $hitLine = -1
            try {
                if ($anchored -and $patternIndex -eq 0) {
                    if ($cursor -lt $lines.Count -and
                            [string]$lines[$cursor] -match $pat) {
                        $hitLine = $cursor
                    }
                } else {
                    for ($lineIndex = $cursor; $lineIndex -lt $lines.Count; $lineIndex++) {
                        if ([string]$lines[$lineIndex] -match $pat) {
                            $hitLine = $lineIndex
                            break
                        }
                    }
                }
            } catch {
                $result.Failures.Add([PSCustomObject]@{
                    Kind = "required_sequence_invalid"
                    Name = $name
                    Message = "invalid regex at step $($patternIndex + 1): $($_.Exception.Message)"
                })
                $failed = $true
                break
            }

            if ($hitLine -lt 0) {
                $result.Failures.Add([PSCustomObject]@{
                    Kind = "required_sequence_mismatch"
                    Name = $name
                    Pattern = $pat
                    Step = $patternIndex + 1
                    Matched = $matched
                    Total = $patterns.Count
                    LastLine = $lastLine
                })
                $failed = $true
                break
            }

            $matched++
            $lastLine = $hitLine + 1
            $cursor = $hitLine + 1
        }

        if (-not $failed) {
            $result.Met++
            if ($VerboseAssertions) {
                Write-Host "  ok  sequence matched: $name ($matched steps)" -ForegroundColor DarkGreen
            }
        } elseif ($VerboseAssertions) {
            Write-Host "  FAIL sequence mismatch: $name ($matched/$($patterns.Count) steps)" -ForegroundColor Red
        }
    }

    # Required counts
    $counts = @()
    if ($Assertions.PSObject.Properties.Match('required_counts').Count -gt 0) {
        $counts = @($Assertions.required_counts)
    }
    foreach ($cspec in $counts) {
        if ($null -eq $cspec) { continue }
        $pat = [string]$cspec.pattern
        if ([string]::IsNullOrWhiteSpace($pat)) { continue }
        $min = 0
        $max = -1
        if ($cspec.PSObject.Properties.Match('min').Count -gt 0) { $min = [int]$cspec.min }
        if ($cspec.PSObject.Properties.Match('max').Count -gt 0) { $max = [int]$cspec.max }

        $count = 0
        foreach ($line in $lines) {
            if ($line -match $pat) { $count++ }
        }
        $result.Total++
        $okMin = ($count -ge $min)
        $okMax = ($max -lt 0) -or ($count -le $max)
        if ($okMin -and $okMax) {
            $result.Met++
            if ($VerboseAssertions) {
                $cap = if ($max -lt 0) { "unbounded" } else { "$max" }
                Write-Host "  ok  count in range [$min..$cap]: $pat -> $count" -ForegroundColor DarkGreen
            }
        } else {
            $result.Failures.Add([PSCustomObject]@{
                Kind = "required_count_out_of_range"
                Pattern = $pat
                Count = $count
                Min = $min
                Max = $max
            })
            if ($VerboseAssertions) {
                Write-Host "  FAIL count out of range: $pat -> $count (min=$min max=$max)" -ForegroundColor Red
            }
        }
    }

    if ($result.Failures.Count -gt 0) {
        $result.Passed = $false
    }
    return $result
}

function Format-AssertionFailures {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [psobject] $Result,
        [int] $LineCap = 200
    )

    $out = New-Object System.Collections.Generic.List[string]
    if (-not $Result.Passed) {
        $out.Add(("  FAILED {0}/{1} assertions" -f ($Result.Total - $Result.Met), $Result.Total))
    } else {
        $out.Add(("  PASS {0}/{1} assertions" -f $Result.Met, $Result.Total))
    }
    $shown = 0
    foreach ($f in $Result.Failures) {
        if ($shown -ge $LineCap) {
            $out.Add(("  ... ({0} more failures truncated)" -f ($Result.Failures.Count - $shown)))
            break
        }
        switch ($f.Kind) {
            "forbidden_pattern_matched" {
                $out.Add(("  - forbidden matched: {0}" -f $f.Pattern))
                if ($f.Line) {
                    $line = $f.Line
                    if ($line.Length -gt 220) { $line = $line.Substring(0, 217) + "..." }
                    $out.Add(("      line: {0}" -f $line))
                }
            }
            "required_line_missing" {
                $out.Add(("  - required missing: {0}" -f $f.Pattern))
            }
            "required_count_out_of_range" {
                $maxStr = if ($f.Max -lt 0) { "unbounded" } else { "$($f.Max)" }
                $out.Add(("  - count out of range: {0} -> {1} (min={2} max={3})" -f $f.Pattern, $f.Count, $f.Min, $maxStr))
            }
            "required_sequence_mismatch" {
                $after = if ($f.LastLine -gt 0) { " after line $($f.LastLine)" } else { " from log start" }
                $out.Add(("  - sequence '{0}' stopped at step {1}/{2}{3}: {4}" -f
                    $f.Name, $f.Step, $f.Total, $after, $f.Pattern))
            }
            "required_sequence_invalid" {
                $out.Add(("  - sequence '{0}' invalid: {1}" -f $f.Name, $f.Message))
            }
            "required_sequence_anchor_missing" {
                $out.Add(("  - sequence '{0}' missing runner anchor: {1}" -f $f.Name, $f.Anchor))
            }
            "log_missing" {
                $out.Add(("  - {0}" -f $f.Message))
            }
            default {
                $out.Add(("  - {0}: {1}" -f $f.Kind, ($f | ConvertTo-Json -Compress)))
            }
        }
        $shown++
    }
    return $out
}
