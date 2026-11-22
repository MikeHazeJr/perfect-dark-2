#Requires -Version 5.1
<#
.SYNOPSIS
    Host the PD2 build queue worker as a scheduled task at Mike's logon.

.DESCRIPTION
    Milestone 5 of the build queue spec checked what Shadowbane offers. NSSM is
    not installed, and a real Windows service needs administrator rights. So the
    worker is hosted the way the spec names as its fallback: a scheduled task
    that starts at logon, runs as Mike (his git credentials push to dev), and
    restarts on failure.

    The task runs the worker from the clean dev clone (C:\PD2\dev). Each landed
    job resets that clone to the new dev, and the worker picks up its own new
    code at its next start.

    A persistent change to this PC: run it only on Mike's say-so.
      .\Install-QueueService.ps1            register and start the task
      .\Install-QueueService.ps1 -Uninstall stop and remove it
#>
param([switch] $Uninstall, [string] $Root = 'C:\PD2')
$ErrorActionPreference = 'Stop'
$taskName = 'PD2 build queue'
$pwsh = 'C:\Program Files\PowerShell\7\pwsh.exe'
$worker = Join-Path $Root 'dev\tools\pd2-queue\Queue-Service.ps1'

if ($Uninstall) {
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
        "Removed the scheduled task '$taskName'."
    } else { "No scheduled task '$taskName'." }
    return
}
if (-not (Test-Path -LiteralPath $pwsh)) { throw "PowerShell 7 is required at $pwsh." }
if (-not (Test-Path -LiteralPath $worker)) { throw "The worker is not in the dev clone yet ($worker); run Queue-Service.ps1 -Setup after tools/pd2-queue is on dev." }
$user = "$env:USERDOMAIN\$env:USERNAME"
# -Root reaches the worker itself: the working directory alone does not choose its queue.
$action = New-ScheduledTaskAction -Execute $pwsh -Argument "-NoProfile -WindowStyle Hidden -File `"$worker`" -Foreground -Root `"$Root`"" -WorkingDirectory $Root
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
$settings = New-ScheduledTaskSettingsSet -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -StartWhenAvailable `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal `
    -Description 'PD2 no-worktree build queue worker (tools/pd2-queue, T-TOOLING-020). One clean build per submitted patch; pushes only refs/heads/dev.' | Out-Null
Start-ScheduledTask -TaskName $taskName
"Registered and started '$taskName' ($worker) for $user. Check it with: pwsh -File $Root\dev\tools\pd2-queue\pd2q.ps1 status"
