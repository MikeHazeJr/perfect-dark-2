#Requires -Version 5.1
<#
.SYNOPSIS
    Assertion engine for the Phase 1 smoke verify gate.

.DESCRIPTION
    Pure log-text inspection. The harness in the PD client writes
    structured log lines; this module re-checks the test definition's
    declared assertions against the resulting file.

    Three assertion families:

      required_lines   -- each regex must match at least one log line.
      forbidden_patterns -- no log line may match any pattern.
      required_counts  -- pattern must match between min and max times
                          (max < 0 means no upper bound).

    Designed to be dot-sourced by run.ps1; no parameters or side effects
    at load time.
#>

Set-StrictMode -Version Latest

function Invoke-SmokeAssertions {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LogPath,
        [Parameter(Mandatory)] [psobject] $Assertions,
        [switch] $Verbose
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

    $lines = Get-Content -LiteralPath $LogPath -ErrorAction SilentlyContinue
    if ($null -eq $lines) { $lines = @() }

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
            if ($Verbose) { Write-Host "  ok  forbidden absent: $pat" -ForegroundColor DarkGreen }
        } else {
            if ($Verbose) { Write-Host "  FAIL forbidden matched: $pat" -ForegroundColor Red }
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
            if ($Verbose) { Write-Host "  ok  required matched: $pat" -ForegroundColor DarkGreen }
        } else {
            $result.Failures.Add([PSCustomObject]@{
                Kind = "required_line_missing"
                Pattern = $pat
            })
            if ($Verbose) { Write-Host "  FAIL required missing: $pat" -ForegroundColor Red }
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
            if ($Verbose) {
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
            if ($Verbose) {
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
