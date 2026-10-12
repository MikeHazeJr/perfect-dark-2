#Requires -Version 5.1
param([Parameter(Mandatory)][string]$EvidenceDirectory,
      [Parameter(Mandatory)][string]$BeforeRunner)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'lib/Storage-Harness.ps1')
. (Join-Path $PSScriptRoot 'lib/Runner-Retention.ps1')
Assert-SmokePlainTree -Path $EvidenceDirectory
$canonical=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=[IO.Path]::GetFullPath($EvidenceDirectory)
if (-not $evidence.StartsWith(($canonical+'\.claude\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Canonical .claude evidence required.' }
if (Test-Path -LiteralPath $evidence) { throw 'Fresh evidence directory required.' }
[IO.Directory]::CreateDirectory($evidence)|Out-Null
$project=Join-Path $evidence 'project'
[IO.Directory]::CreateDirectory($project)|Out-Null
$policy=@{schema=1;min_free_gib=0;max_full_installs=5;max_full_install_gib=.01;
    max_archive_gib=.01;peak_run_gib=0;extra_growth_gib=0;peak_build_gib=0}
$StoragePolicy=Join-Path $evidence 'fixture-policy.json'
$policy|ConvertTo-Json|Set-Content -LiteralPath $StoragePolicy -Encoding utf8
$checks=New-Object 'Collections.Generic.List[string]'
function Check([bool]$Passed,[string]$Label) {
    if (-not $Passed) { throw "Check failed: $Label" }
    $checks.Add($Label)
}
function Refuses([scriptblock]$Action,[string]$Pattern,[string]$Label) {
    $refused=$false
    try { & $Action|Out-Null } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        $refused=$true
    }
    Check $refused $Label
}
$runner=Join-Path $PSScriptRoot 'run.ps1'
# Execute actual entrypoint negative paths. All refuse before bootstrap/native calls.
Refuses { & $runner -RetainHousekeeping -Build -PerTestInstall } 'frozen build' 'early Build refusal'
Refuses { & $runner -RetainHousekeeping -SharedInstall } 'SharedInstall' 'early shared refusal'
Refuses { & $runner -RetainHousekeeping } 'PerTestInstall' 'explicit private mode required'
Assert-SmokeRunnerRetentionOptions -Retained $false -BuildRequested $true -SharedRequested $true `
    -PrivateRequested $false -TemplateRequested $false -ReuseRequested $false
$checks.Add('default options remain accepted')

$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($runner,[ref]$tokens,[ref]$errors)
Check (@($errors).Count -eq 0) 'runner parses'
$beforeTokens=$null;$beforeErrors=$null
$before=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($BeforeRunner),[ref]$beforeTokens,[ref]$beforeErrors)
Check (@($beforeErrors).Count -eq 0) 'baseline parses'
$parameters=@($ast.ParamBlock.Parameters|ForEach-Object {$_.Name.VariablePath.UserPath})
Check ($parameters -contains 'RetainHousekeeping') 'canonical option exists'

# Execute the exact loading/selection statements from both production paths.
$loads=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.InvocationOperator -eq [Management.Automation.Language.TokenKind]::Dot -and
    $node.Extent.Text -match "Join-Path \`$LibDir ['`"](?:Storage|Install)-Harness.ps1"},$true)|Sort-Object {$_.Extent.StartOffset})
$selects=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Set-SmokeStorageRetention'},$true)|Sort-Object {$_.Extent.StartOffset})
$initializers=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Initialize-SmokeStoragePolicy'},$true)|Sort-Object {$_.Extent.StartOffset})
Check ($loads.Count -eq 2 -and $selects.Count -eq 2 -and $initializers.Count -eq 2) 'both real loading seams selected'
$LibDir=Join-Path $PSScriptRoot 'lib';$RetainHousekeeping=$true
for($i=0;$i -lt 2;$i++) {
    Check ($loads[$i].Extent.StartOffset -lt $selects[$i].Extent.StartOffset -and
        $selects[$i].Extent.StartOffset -lt $initializers[$i].Extent.StartOffset) "selection order seam$i"
    . ([scriptblock]::Create($loads[$i].Extent.Text))
    . ([scriptblock]::Create($selects[$i].Extent.Text))
    . ([scriptblock]::Create($initializers[$i].Extent.Text))
    $status=Invoke-SmokeStorage -Action status -ProjectRoot $project
    Check ($status.Retention.retain_housekeeping -eq $true) "native backend receives retention seam$i"
}
Check (-not (Test-Path -LiteralPath (Join-Path $project '.claude/smoke-storage'))) 'bootstrap checks create no storage state'
$earlyReuse=$ast.FindAll({param($node) $node -is [Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -eq '$ReuseInstallId'},$true)|Sort-Object {$_.Extent.StartOffset}|Select-Object -First 1
Check ($earlyReuse.Extent.Text -match 'if \(\$ReusePlan\)' -and $earlyReuse.Extent.Text -match 'return' -and
    $earlyReuse.Extent.Text -notmatch 'New-SmokeRunnerRetentionContext|New-SmokeReadOnlyProfile') 'early ReusePlan remains before context/lease allocation'

function FunctionNode($Tree,[string]$Name) {
    return $Tree.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $Name},$true)
}
foreach($name in @('New-SmokeRetainedLaunch','Close-SmokeRetainedLaunch','Get-SmokeLaunchCloseoutSummary',
    'Stop-SmokeOwnedFaultProcesses','Wait-AppendOnlyLogWitness','Invoke-PendingSmokeScreenshots',
    'New-SmokeScreenshotSchedule','Invoke-SmokeTestMultiProcess')) {
    $old=FunctionNode $before $name; $current=FunctionNode $ast $name
    Check ($old.Extent.Text.Replace("`r`n","`n") -ceq $current.Extent.Text.Replace("`r`n","`n")) "excluded function unchanged: $name"
}
$oldTry=@($before.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.TryStatementAst]})[-1]
$newOuter=@($ast.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.TryStatementAst]})[-1]
$newTry=$newOuter.Body.Statements[0]
Check ($oldTry.Finally.Extent.Text.Replace("`r`n","`n") -ceq $newTry.Finally.Extent.Text.Replace("`r`n","`n")) 'outer owned-process closeout unchanged'

# A successful result can precede a failing finally. Execute the production
# catch verbatim in a tiny child so exit/finally semantics are exercised too.
$closeoutProject=Join-Path $evidence 'closeout-project'
[IO.Directory]::CreateDirectory($closeoutProject)|Out-Null
$childScript=Join-Path $evidence 'closeout-regression.ps1'
$childText=@'
param([string]$LibDir,[string]$ProjectRoot,[string]$PolicyFile)
$ErrorActionPreference='Stop'
. (Join-Path $LibDir 'Storage-Harness.ps1')
. (Join-Path $LibDir 'Runner-Retention.ps1')
$script:SmokeStoragePolicy=Get-Content -LiteralPath $PolicyFile -Raw|ConvertFrom-Json
$RetainHousekeeping=$true
$script:RunnerRetentionContext=New-SmokeRunnerRetentionContext -ProjectRoot $ProjectRoot
try {
    try {
        [void](Write-SmokeRunnerRetainedJson -Context $script:RunnerRetentionContext -Name 'fixture-passed.json' -Value @{passed=$true})
        exit 0
    } finally {
        throw 'Outer launch closeout unverified; synthetic regression'
    }
}
'@
$childText+="`n"+$newOuter.CatchClauses[0].Extent.Text+"`n"
[IO.File]::WriteAllText($childScript,$childText,(New-Object Text.UTF8Encoding($false)))
$childLog=Join-Path $evidence 'closeout-child.log'
& (Get-Process -Id $PID).Path -NoProfile -NonInteractive -File $childScript -LibDir $LibDir `
    -ProjectRoot $closeoutProject -PolicyFile $StoragePolicy *> $childLog
Check ($LASTEXITCODE -ne 0) 'finally failure overrides prior success exit'
$childRoots=@(Get-ChildItem -LiteralPath (Join-Path $closeoutProject '.claude/smoke-storage/receipts') -Directory -Filter 'runner-*')
Check ($childRoots.Count -eq 1) 'closeout fixture has one fresh owned run'
$priorResult=Get-Content -LiteralPath (Join-Path $childRoots[0].FullName 'fixture-passed.json') -Raw|ConvertFrom-Json
Check ($priorResult.passed -eq $true) 'published result survives closeout failure'
$failureFiles=@(Get-ChildItem -LiteralPath $childRoots[0].FullName -File -Filter 'runner-error-*.json')
Check ($failureFiles.Count -eq 1) 'closeout failure has distinct retained receipt'
$failure=Get-Content -LiteralPath $failureFiles[0].FullName -Raw|ConvertFrom-Json
Check ($failure.passed -eq $false -and $failure.error -match 'Outer launch closeout unverified') 'failure receipt identifies unverified closeout'

# Actual function's guard refuses unintegrated mutations before dispatch/setup.
$invoke=FunctionNode $ast 'Invoke-SmokeTest'
. ([scriptblock]::Create($invoke.Extent.Text))
foreach($property in @('processes','remove_paths','fixtures','packed_fixtures','catalog_ingress_boundary_fixtures',
        'needler_effect_replacement_fixtures','v006_corrupt_source_fixtures','weapon_mesh_ingress_fixtures','temporary_recovery_fixtures')) {
    $definition=[pscustomobject]@{$property=@('inert')}
    Refuses { Invoke-SmokeTest -Test ([pscustomobject]@{Name='inert';Definition=$definition}) } 'fixture housekeeping' "before-setup refusal: $property"
}
Refuses { Invoke-SmokeTest -Shared -Test ([pscustomobject]@{Name='inert';Definition=[pscustomobject]@{}}) } 'shared install setup' 'direct shared setup refused'

$script:SmokeStoragePolicy=$policy
$context=New-SmokeRunnerRetentionContext -ProjectRoot $project
$written=Write-SmokeRunnerRetainedJson -Context $context -Name 'result.json' -Value @{passed=$false;label='café'}
Check ($written.Retention.retain_housekeeping -eq $true -and $written.AccountingDomain -eq 'archive') 'real writer confirms archive domain and retention'
$saved=Get-Content -LiteralPath $written.Path -Raw|ConvertFrom-Json
Check ($saved.passed -eq $false -and $saved.label -eq 'café') 'failure verdict and Unicode preserved'
Refuses { Write-SmokeRunnerRetainedJson -Context $context -Name 'result.json' -Value @{passed=$true} } 'output collision' 'existing result cannot be overwritten'
Check ((Get-Content -LiteralPath $written.Path -Raw|ConvertFrom-Json).passed -eq $false) 'prior failed result remains failed'
Refuses { Get-SmokeRunnerRetentionEvidence -Context $context -StorageReceipt ([pscustomobject]@{}) } 'did not confirm' 'missing backend confirmation refused'
$evidenceRow=Get-SmokeRunnerRetentionEvidence -Context $context -StorageReceipt ([pscustomobject]@{Retention=[pscustomobject]@{retain_housekeeping=$true;operation_id='fixture'}})
Check ($evidenceRow.RunId -eq $context.RunId) 'run identity separate from backend operation identity'

foreach($kind in @('game-log','ownership','console')) {
    $install=Join-Path $project ('install-'+$kind)
    [IO.Directory]::CreateDirectory($install)|Out-Null
    Assert-SmokeRetainedLaunchOutputs -InstallDir $install
    if($kind -eq 'game-log') {[IO.File]::WriteAllText((Join-Path $install 'pd-client.log'),'prior log')}
    else {$leaf=if($kind -eq 'ownership'){'logs/smoke-process-ownership'}else{'logs/smoke-console'};[IO.Directory]::CreateDirectory((Join-Path $install $leaf))|Out-Null}
    Refuses { Assert-SmokeRetainedLaunchOutputs -InstallDir $install } 'collision' "launch collision: $kind"
}
@{passed=$true;checks=$checks.ToArray();count=$checks.Count;run_id=$context.RunId;
    no_game_launch=$true;all_fixtures_retained=$true;PS51='not executed; prior AuthorizationManager block retained'}|
    ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding utf8
Write-Output "PASS: $($checks.Count) runner plumbing checks; no game or fixture cleanup."
