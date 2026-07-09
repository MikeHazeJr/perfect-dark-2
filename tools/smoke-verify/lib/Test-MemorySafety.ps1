<#
    Test-MemorySafety.ps1 -- B-801 memory-risk control for live smoke launches.

    B-801 (bugs.md) was NOT an application crash: the Windows Event Log showed a
    real host-level virtual-memory exhaustion event while a live Scenario smoke was
    running -- Kernel-Power 41, bugcheck 0x000000ef, repeated "paging file is too
    small" / low-virtual-memory errors, stornvme allocation warnings, and failures
    cascading across DWM / Git / NVIDIA / PowerToys. Root cause: a 2 GB pagefile
    (too small for the game + assets + concurrent dev tooling), so the system ran
    out of COMMIT and took the machine (and the git index) down with it.

    The bug entry states live testing must not resume "until bounded proof and
    memory-risk controls exist." This is that control: a read-only preflight that
    measures available commit + pagefile size and REFUSES to launch a live game
    process when headroom is dangerously low. It changes no system settings and
    launches nothing -- it only reads counters and returns a verdict.

    Thresholds are env-overridable so an operator who has fixed their pagefile can
    tune or disable the gate:
      PD_SMOKE_MIN_FREE_COMMIT_MB  (default 2048) -- min available commit to launch
      PD_SMOKE_MIN_PAGEFILE_MB     (default 4096) -- min pagefile (B-801 was 2048)
      PD_SMOKE_SKIP_MEMORY_GUARD=1                -- bypass the gate entirely

    Fail-OPEN on measurement error: if the CIM probe is unavailable (locked-down
    host), we do not block -- the danger this guards against (low commit / tiny
    pagefile) is measurable on the target host, and blocking all CI on a probe
    failure would be worse than the risk. Measured-unsafe DOES block.
#>

function Test-SmokeMemorySafety {
    [CmdletBinding()]
    param(
        [int]$MinFreeCommitMB = 0,
        [int]$MinPagefileMB = 0
    )

    if ($MinFreeCommitMB -le 0) {
        $MinFreeCommitMB = 2048
        if ($env:PD_SMOKE_MIN_FREE_COMMIT_MB) {
            $parsed = 0
            if ([int]::TryParse($env:PD_SMOKE_MIN_FREE_COMMIT_MB, [ref]$parsed)) { $MinFreeCommitMB = $parsed }
        }
    }
    if ($MinPagefileMB -le 0) {
        $MinPagefileMB = 4096
        if ($env:PD_SMOKE_MIN_PAGEFILE_MB) {
            $parsed = 0
            if ([int]::TryParse($env:PD_SMOKE_MIN_PAGEFILE_MB, [ref]$parsed)) { $MinPagefileMB = $parsed }
        }
    }

    $result = [PSCustomObject]@{
        Safe        = $true
        Reasons     = @()
        Info        = @()
        Remediation = ""
    }

    if ($env:PD_SMOKE_SKIP_MEMORY_GUARD -eq "1") {
        $result.Info += "memory guard bypassed (PD_SMOKE_SKIP_MEMORY_GUARD=1)"
        return $result
    }

    try {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
        $freeCommitMB  = [math]::Round([double]$os.FreeVirtualMemory / 1024)
        $totalCommitMB = [math]::Round([double]$os.TotalVirtualMemorySize / 1024)
        $freePhysMB    = [math]::Round([double]$os.FreePhysicalMemory / 1024)
        $result.Info += ("free commit {0} MB / {1} MB limit; free RAM {2} MB" -f $freeCommitMB, $totalCommitMB, $freePhysMB)

        if ($freeCommitMB -lt $MinFreeCommitMB) {
            $result.Safe = $false
            $result.Reasons += ("available commit {0} MB is below the {1} MB floor -- B-801 crashed the host by exhausting commit" -f $freeCommitMB, $MinFreeCommitMB)
        }
    } catch {
        # Fail-open on probe failure (see header): cannot measure, do not block.
        $result.Info += ("commit probe unavailable, not blocking: {0}" -f $_.Exception.Message)
    }

    try {
        # B-801 was a FIXED 2 GB pagefile that could not grow under commit
        # pressure. A SYSTEM-MANAGED pagefile grows on demand, so its current
        # AllocatedBaseSize (often small while demand is low) must NOT be held
        # against the static floor -- that misfired on healthy hosts (refused
        # to launch with 18 GB RAM free because the auto pagefile happened to
        # sit at 3.4 GB). Apply the floor only to manually-sized pagefiles,
        # and to their configured MAXIMUM (the growth ceiling B-801 actually
        # hit), not the current allocation. The commit-headroom check above
        # remains the real gate either way.
        $autoManaged = $false
        try {
            $cs = Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
            $autoManaged = [bool]$cs.AutomaticManagedPagefile
        } catch {}

        if ($autoManaged) {
            $pfUsage = @(Get-CimInstance -ClassName Win32_PageFileUsage -ErrorAction SilentlyContinue)
            $pfMB = 0
            foreach ($p in $pfUsage) { $pfMB += [int]$p.AllocatedBaseSize }
            $result.Info += ("pagefile system-managed (grows on demand; currently {0} MB) -- static floor not applied" -f $pfMB)
        } else {
            # Manually-sized pagefile(s): judge by the configured MAXIMUM.
            # MaximumSize 0 in Win32_PageFileSetting means "system managed
            # size" for that file -- treat as growable.
            $pfSettings = @(Get-CimInstance -ClassName Win32_PageFileSetting -ErrorAction Stop)
            $maxMB = 0
            $anyGrowable = $false
            foreach ($p in $pfSettings) {
                if ([int]$p.MaximumSize -eq 0) { $anyGrowable = $true }
                $maxMB += [int]$p.MaximumSize
            }
            if ($pfSettings.Count -gt 0 -and -not $anyGrowable -and $maxMB -gt 0) {
                $result.Info += ("pagefile fixed maximum {0} MB" -f $maxMB)
                if ($maxMB -lt $MinPagefileMB) {
                    $result.Safe = $false
                    $result.Reasons += ("fixed pagefile maximum {0} MB is below the {1} MB floor -- B-801 root cause was a fixed 2 GB pagefile" -f $maxMB, $MinPagefileMB)
                }
            } else {
                # No settings rows / growable entries: cannot prove a fixed
                # small ceiling. Do not block; the commit check is the gate.
                $result.Info += "pagefile growable or size not reported -- static floor not applied"
            }
        }
    } catch {
        $result.Info += ("pagefile probe unavailable: {0}" -f $_.Exception.Message)
    }

    if (-not $result.Safe) {
        $result.Remediation = "Increase the Windows pagefile (System > Advanced system settings > Performance > Advanced > Virtual memory) to >= 8 GB or system-managed, and/or close other memory-heavy apps, then retry. Bypass at your own risk with PD_SMOKE_SKIP_MEMORY_GUARD=1. See B-801 in context/bugs.md."
    }

    return $result
}
