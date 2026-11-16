# Runner plumbing only; existing storage owns locks, budgets and reservations.
. (Join-Path $PSScriptRoot 'Runner-Supervisor.ps1')
$loadedPath=[IO.Path]::GetFullPath($PSCommandPath)
$loadedHash=(Get-FileHash -LiteralPath $loadedPath -Algorithm SHA256).Hash.ToLowerInvariant()
if($script:SmokeReceiptLoadedSources.ContainsKey($loadedPath) -and $script:SmokeReceiptLoadedSources[$loadedPath] -cne $loadedHash){throw 'Runner retention module changed in a live interpreter; preserve its original operations.'}
$script:SmokeReceiptLoadedSources[$loadedPath]=$loadedHash
if($null -eq (Get-Variable -Name SmokeRunnerSupervisorOperations -Scope Script -ErrorAction SilentlyContinue)){
    $script:SmokeRunnerSupervisorOperations=@{}
}

function Write-SmokeRunnerSupervisedJson {
    param([Parameter(Mandatory)]$Context,[Parameter(Mandatory)][string]$Name,[Parameter(Mandatory)]$Value,$OwnerRecord=$null)
    $descriptor=New-SmokeRunnerSupervisorDescriptor $Context $Name $Value
    $id=$descriptor.Spec.operation
    if($script:SmokeRunnerSupervisorOperations.ContainsKey($id)){
        $operation=$script:SmokeRunnerSupervisorOperations[$id]
        if($operation.Descriptor.Spec.request_sha256 -cne $descriptor.Spec.request_sha256){throw 'Receipt supervisor operation payload changed; never redispatch.'}
        foreach($key in $descriptor.Spec.sources.Keys){if($operation.Descriptor.Spec.sources.$key -cne $descriptor.Spec.sources[$key]){throw 'Receipt supervisor operation source changed; preserve existing owner.'}}
    }else{
        $operation=Start-SmokeRunnerReceiptOperation $descriptor
        $script:SmokeRunnerSupervisorOperations[$id]=$operation
    }
    if($null -ne $OwnerRecord){$OwnerRecord.SupervisorOperation=$operation}
    # Called in the tracked parent runspace. Finite caller timeout retains it;
    # its native supervisor/context/broker independently survives wrapper exit.
    while(-not $operation.Complete){
        $result=Read-SmokeRunnerReceiptOperation $operation
        if($null -ne $result){return $result}
        Start-Sleep -Milliseconds 2
    }
    return $operation.Result
}
function Assert-SmokeRunnerRetentionOptions {
    param([bool]$Retained, [bool]$BuildRequested, [bool]$SharedRequested,
          [bool]$PrivateRequested, [bool]$TemplateRequested, [bool]$ReuseRequested)
    if (-not $Retained) { return }
    if ($BuildRequested) { throw 'Retained runner requires an already frozen build; Build housekeeping is not integrated.' }
    if ($SharedRequested) { throw 'Retained runner refuses SharedInstall reset.' }
    if (-not $ReuseRequested -and -not $PrivateRequested -and -not $TemplateRequested) {
        throw 'Retained runner requires explicit PerTestInstall or a read-only Install template.'
    }
}

function Assert-SmokeRetainedDefinition {
    param([Parameter(Mandatory)]$Definition)
    foreach ($property in @('processes','remove_paths','fixtures','packed_fixtures',
            'catalog_ingress_boundary_fixtures','needler_effect_replacement_fixtures',
            'v006_corrupt_source_fixtures','weapon_mesh_ingress_fixtures','temporary_recovery_fixtures')) {
        if ($Definition.PSObject.Properties.Match($property).Count -and $Definition.$property) {
            throw "Retained runner fixture housekeeping '$property' requires a separate preserved-fixture integration; no install or launch admitted."
        }
    }
}

function Invoke-SmokeRunnerRetention {
    param([Parameter(Mandatory)][ValidateSet('new-run','write-run-json','write-profile-json')][string]$Action,
          [Parameter(Mandatory)][string]$ProjectRoot, [hashtable]$Request=@{})
    $Request.project=$ProjectRoot; $Request.policy=$script:SmokeStoragePolicy; $Request.owner_pid=$PID
    $process=New-Object Diagnostics.Process
    try {
        $utf8=New-Object Text.UTF8Encoding($false)
        $start=New-Object Diagnostics.ProcessStartInfo
        $start.FileName=Get-SmokeStoragePython
        $backend=Join-Path (Split-Path -Parent $PSScriptRoot) 'runner_retention.py'
        $start.Arguments='-X utf8 -B "'+$backend+'" '+$Action
        $start.UseShellExecute=$false; $start.CreateNoWindow=$true
        $start.RedirectStandardInput=$true; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
        $start.StandardOutputEncoding=$utf8; $start.StandardErrorEncoding=$utf8
        $process.StartInfo=$start
        if (-not $process.Start()) { throw 'Retained runner helper did not start.' }
        $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
        $bytes=$utf8.GetBytes((ConvertTo-Json -InputObject $Request -Depth 50 -Compress))
        $process.StandardInput.BaseStream.Write($bytes,0,$bytes.Length); $process.StandardInput.Close()
        $process.WaitForExit()
        $output=$stdout.GetAwaiter().GetResult(); $errors=$stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Retained runner $Action refused (exit $($process.ExitCode)); evidence preserved. $($errors.Trim())" }
        return ($output | ConvertFrom-Json)
    } finally { $process.Dispose() }
}

function New-SmokeRunnerRetentionContext {
    param([Parameter(Mandatory)][string]$ProjectRoot)
    return Invoke-SmokeRunnerRetention -Action new-run -ProjectRoot $ProjectRoot
}

function Write-SmokeRunnerRetainedJson {
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)][string]$Name,
          [Parameter(Mandatory)]$Value)
    return Invoke-SmokeRunnerRetention -Action write-run-json -ProjectRoot $Context.ProjectRoot -Request @{
        run_id=$Context.RunId; name=$Name; value=$Value
    }
}

function Write-SmokeRunnerRetainedProfileResult {
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)][string]$ReuseId,
          [Parameter(Mandatory)]$Value)
    return Invoke-SmokeRunnerRetention -Action write-profile-json -ProjectRoot $Context.ProjectRoot -Request @{
        run_id=$Context.RunId; reuse_id=$ReuseId; value=$Value
    }
}

function Assert-SmokeRetainedLaunchOutputs {
    param([Parameter(Mandatory)][string]$InstallDir, [string[]]$ScreenshotPaths=@())
    foreach ($leaf in @('pd-client.log','pd-host.log','pd-server.log')) {
        foreach ($path in (Get-SmokeLogCandidatePaths -InstallDir $InstallDir -Leaf $leaf)) {
            Assert-SmokePlainTree -Path $path
            if (Test-Path -LiteralPath $path) { throw "Retained runner game-log collision: $path" }
        }
    }
    foreach ($relative in @('logs/smoke-process-ownership','logs/smoke-console')) {
        $path=Join-Path $InstallDir $relative
        Assert-SmokePlainTree -Path $path -Descendants
        if (Test-Path -LiteralPath $path) { throw "Retained runner launch-output collision: $path" }
    }
    foreach ($path in $ScreenshotPaths) {
        Assert-SmokePlainTree -Path $path
        if (Test-Path -LiteralPath (Split-Path -Parent $path)) {
            throw "Retained runner screenshot-output collision: $path"
        }
    }
}

function Get-SmokeRunnerRetentionEvidence {
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)]$StorageReceipt)
    if (-not $StorageReceipt.PSObject.Properties.Match('Retention').Count -or
            $StorageReceipt.Retention.retain_housekeeping -ne $true) {
        throw 'Storage completion did not confirm effective retained housekeeping.'
    }
    return [pscustomobject]@{RunId=$Context.RunId;RunRoot=$Context.RunRoot;Storage=$StorageReceipt.Retention}
}
