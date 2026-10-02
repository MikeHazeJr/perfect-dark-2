# File-backed exact native smoke cancellation; never controls app input/grants.
param([Parameter(Mandatory)][string]$OwnershipFile)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
. (Join-Path $PSScriptRoot 'lib/Owned-Process.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$storage = [IO.Path]::GetFullPath((Join-Path $root '.claude/smoke-storage'))
$file = ConvertTo-SmokeProcessPath $OwnershipFile
Assert-SmokePlainTree -Path $file
if ((Get-Item -LiteralPath $file).Length -gt 8192) { throw 'Oversized process ownership record' }
if ([IO.Path]::GetFileName($file) -notmatch '^owned-\d+\.json$') { throw 'Unexpected ownership record name' }
$ownershipDirectory = [IO.Path]::GetDirectoryName($file)
$logsDirectory = [IO.Path]::GetDirectoryName($ownershipDirectory)
$installDirectory = [IO.Path]::GetDirectoryName($logsDirectory)
if ([IO.Path]::GetFileName($ownershipDirectory) -ne 'smoke-process-ownership' -or
        [IO.Path]::GetFileName($logsDirectory) -ne 'logs') { throw 'Record must belong to the private install log directory' }
$allowed = $false
foreach ($area in @('shared', 'installs', 'restored')) {
    $prefix = (Join-Path $storage $area) + [IO.Path]::DirectorySeparatorChar
    if ($installDirectory.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { $allowed = $true }
}
if (-not $allowed) { throw 'Record outside managed private installs' }
$ownership = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
$marker = Get-Content -LiteralPath (Join-Path $installDirectory '.pd-storage-owner.json') -Raw | ConvertFrom-Json
if (-not $marker.prospective -or -not $ownership.storage_install_id -or
        $marker.id -ne $ownership.storage_install_id) { throw 'Private install ownership changed; preserve and review' }
$expectedExecutable = Join-Path $installDirectory 'PerfectDark.exe'
if (-not [string]::Equals((ConvertTo-SmokeProcessPath $ownership.executable_path),
        $expectedExecutable, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($file) -ne ("owned-{0}.json" -f [int]$ownership.process_id)) {
    throw 'Ownership file/executable mismatch'
}
Stop-SmokeOwnedProcess -Ownership $ownership | ConvertTo-Json
