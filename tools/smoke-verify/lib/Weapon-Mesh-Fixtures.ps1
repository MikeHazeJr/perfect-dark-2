function New-WeaponMeshIngressFixtures {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $InstallDir,
        [switch] $IncludeBaseFalcon2
    )
    $generator = 'tools/smoke-verify/generate_weapon_mesh_ingress_fixtures.py'
    # Both Windows and build-environment MSYS Python accept these paths.
    $relativeInstall = [System.IO.Path]::GetRelativePath(
        $ProjectRoot, $InstallDir).Replace('\', '/')
    $fixtureArgs = @($generator, '--install-dir', $relativeInstall)
    if ($IncludeBaseFalcon2) { $fixtureArgs += '--include-base-falcon2' }
    Push-Location $ProjectRoot
    try {
        & python @fixtureArgs
        if ($LASTEXITCODE -ne 0) {
            throw 'Weapon mesh ingress fixture generation failed'
        }
    } finally {
        Pop-Location
    }
}
