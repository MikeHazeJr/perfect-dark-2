#Requires -Version 5.1
param([Parameter(Mandatory)][string] $EvidenceDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
Assert-SmokePlainTree -Path $EvidenceDirectory
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence = [IO.Path]::GetFullPath($EvidenceDirectory)
if (-not $evidence.StartsWith(($root + '\.claude\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence must be inside canonical .claude.'
}
if (Test-Path -LiteralPath $evidence) { throw 'Fresh evidence destination required.' }
[IO.Directory]::CreateDirectory($evidence) | Out-Null
$project = Join-Path $evidence 'project'
[IO.Directory]::CreateDirectory($project) | Out-Null
$checks = New-Object 'System.Collections.Generic.List[string]'
function Expect-Refusal([hashtable]$Request) {
    $refused = $false
    try { Invoke-SmokeStorage -Action status -ProjectRoot $project -Request $Request | Out-Null }
    catch { $refused = $true; $checks.Add($_.Exception.Message) }
    if (-not $refused) { throw 'Expected retention request refusal.' }
}
Expect-Refusal @{retention=$null}
Expect-Refusal @{retention=@{unexpected=$true}}
Expect-Refusal @{retention=@{retain_housekeeping=1}}
Expect-Refusal @{retention=@{Retain_Housekeeping=$true}}
Set-SmokeStorageRetention -RetainHousekeeping
Initialize-SmokeStoragePolicy
Expect-Refusal @{retention=@{retain_housekeeping=$false}}
$result = Invoke-SmokeStorage -Action status -ProjectRoot $project
if ($result.Retention.retain_housekeeping -ne $true -or -not $result.Retention.operation_id) {
    throw 'Initialization lost selected retention or operation identity.'
}
if (Test-Path -LiteralPath (Join-Path $project '.claude/smoke-storage')) {
    throw 'Read-only validation created storage state.'
}
Set-SmokeStorageRetention
$default = Invoke-SmokeStorage -Action status -ProjectRoot $project
if ($default.PSObject.Properties.Name -contains 'Retention') { throw 'Default unexpectedly selected retention.' }
@{checks=$checks.ToArray();selected=$result.Retention;default_unchanged=$true;fixture_retained=$true} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding utf8
Write-Output 'PASS: five malformed/downgrade refusals, initialization persistence, operation identity, default selection and no storage mutation.'
