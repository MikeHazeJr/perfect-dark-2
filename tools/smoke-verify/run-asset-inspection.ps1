#requires -Version 5.1
<# Read-only planner / bounded ledger driver. Does not launch captures or copy installs. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string[]] $AssetRoot,
    [string] $OutputDirectory = '',
    [string] $Receipts = '',
    [string] $BinarySha256 = '',
    [string] $SourceSha256 = '',
    [string] $StoragePolicy = '',
    [long] $MaxHashBytes = 2147483648,
    [long] $MaxLedgerBytes = 67108864,
    [int] $CaptureCount = 0,
    [long] $BytesPerCapture = 33554432,
    [switch] $Inventory,
    [switch] $DryRun
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proofRoot = [IO.Path]::GetFullPath((Join-Path $repo '.claude\asset-inspection'))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $proofRoot ((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ'))
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $output.StartsWith($proofRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output must be a new owned child of .claude/asset-inspection'
}
if ($CaptureCount -lt 0 -or $BytesPerCapture -le 0 -or $MaxHashBytes -le 0 -or $MaxLedgerBytes -le 0) { throw 'Invalid budget' }
$roots = @($AssetRoot | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
if (-not $StoragePolicy) { $StoragePolicy = Join-Path $PSScriptRoot 'storage-policy.json' }
$policyPath = (Resolve-Path -LiteralPath $StoragePolicy).Path
$policy = Get-Content -LiteralPath $policyPath -Raw | ConvertFrom-Json
if ([decimal]$policy.min_free_gib -lt 0 -or [decimal]$policy.extra_growth_gib -lt 0) { throw 'Invalid storage policy' }
$drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($output))
$captureBytes = [decimal]$CaptureCount * $BytesPerCapture
$ledgerBytes = [decimal]$MaxLedgerBytes
$spoolBytes = [decimal]$MaxHashBytes
$floorBytes = [decimal]$policy.min_free_gib * 1GB
$required = $floorBytes + [decimal]$policy.extra_growth_gib * 1GB + $captureBytes + $ledgerBytes + $spoolBytes + 4096 + 65536
$conformanceArguments = @()
foreach ($root in $roots) { $conformanceArguments += @('--root', $root) }
$tools = @(
    @{tool='tools/asset_archive_conformance.py';arguments=$conformanceArguments;role='schema and nested dependencies'},
    @{tool='tools/verify_pdmesh_sources.py';arguments=$roots;role='editable model geometry'},
    @{tool='tools/verify_pdmesh_render_state.py';arguments=@('--help');argument_template=@('--expect-lighting','<pdmesh-path>=on|off|inherited|baseline|mixed');role='requires reviewed per-model expected lighting; --help only is immediately runnable'},
    @{tool='tools/verify_audio_sources.py';arguments=$roots;role='public waveform timing'},
    @{tool='tools/smoke-verify/run-scenario-source-matrix.ps1';arguments=@('-All');role='native world receipts; separately coordinated runtime'},
    @{tool='tools/smoke-verify/run.ps1';arguments=@('-Test','audio_live_playback_source_smoke','-StoragePolicy',$policyPath);role='native audible playback fixture; separately coordinated runtime'},
    @{tool='tools/asset_native_source_guard.py';arguments=@();role='public source invariant'}
)
$plan = [ordered]@{
    schema='pd2.asset-inspection-plan.v1';timestamp=(Get-Date).ToUniversalTime().ToString('o')
    roots=$roots;output=$output;mode=$(if ($Inventory -and -not $DryRun) {'inventory'} else {'dry_run'})
    storage_policy=$policyPath
    budgets=@{max_hash_bytes=$MaxHashBytes;max_ledger_bytes=$MaxLedgerBytes;reserved_spool_bytes=$spoolBytes;reserved_failure_receipt_bytes=4096;max_plan_bytes=65536;estimated_ledger_bytes=$ledgerBytes;capture_count=$CaptureCount;bytes_per_capture=$BytesPerCapture;estimated_capture_bytes=$captureBytes;min_free_bytes=$floorBytes;required_free_bytes=$required;actual_free_bytes=$drive.AvailableFreeSpace;preflight_pass=($drive.AvailableFreeSpace -ge $required)}
    tools=$tools;native_seam='Per-asset catalog/provider receipt bound to archive hash and binary/source generation; explicit family behavioral checks and hashed native trace. Missing seam remains pending.'
    runtime_started=$false;install_copies=0
}
$planJson = $plan | ConvertTo-Json -Depth 10
if ([Text.Encoding]::UTF8.GetByteCount($planJson) + 5 -gt 65536) { throw 'Plan exceeds reserved 64 KiB; no files created' }
if ($DryRun -or -not $Inventory) { $planJson; return }
$pythonExe = Get-SmokeStoragePython
if (-not $plan.budgets.preflight_pass) { throw 'Storage preflight refused inventory output; no files created' }
if (Test-Path -LiteralPath $output) { throw 'Output directory already exists; preserve receipts' }
New-Item -ItemType Directory -Path $output | Out-Null
$planJson | Set-Content -LiteralPath (Join-Path $output 'plan.json') -Encoding UTF8
$arguments = @((Join-Path $repo 'tools\asset-inspection-manifest.py')) + $roots + @('--output',(Join-Path $output 'ledger.json'),'--max-bytes',"$MaxHashBytes",'--max-output-bytes',"$MaxLedgerBytes")
if ($BinarySha256) { $arguments += @('--binary-sha256',$BinarySha256) }
if ($SourceSha256) { $arguments += @('--source-sha256',$SourceSha256) }
if ($Receipts) { $arguments += @('--receipts', (Resolve-Path -LiteralPath $Receipts).Path) }
& $pythonExe -B @arguments
if ($LASTEXITCODE -ne 0) { throw "Incomplete inventory; retained ledger at $output" }
Write-Output $output
