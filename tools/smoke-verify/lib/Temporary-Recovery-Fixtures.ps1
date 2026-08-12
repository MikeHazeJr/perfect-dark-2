function New-TemporaryRecoveryFixtures {
    [CmdletBinding()] param(
        [Parameter(Mandatory)] [string] $ProjectRoot,
        [Parameter(Mandatory)] [string] $InstallDir
    )

    $source = Join-Path $ProjectRoot `
        'examples\modding\typed-pdxxx-basic\weapons\tri_weapon.pdweapon'
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Temporary recovery source archive missing: $source"
    }

    $fixtureRoot = Join-Path $InstallDir 'social\temporary-recovery'
    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    $pdcaPath = Join-Path $fixtureRoot 'tri_weapon_recovery.pdca'
    $memberName = 'weapons/tri_weapon.pdweapon'
    $memberPathBytes = [System.Text.Encoding]::UTF8.GetBytes($memberName)
    $memberBytes = [System.IO.File]::ReadAllBytes($source)

    $stream = [System.IO.File]::Open($pdcaPath,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None)
    try {
        $writer = [System.IO.BinaryWriter]::new($stream,
            [System.Text.Encoding]::UTF8, $true)
        try {
            $writer.Write([uint32]0x41434450)
            $writer.Write([uint16]1)
            $writer.Write([uint16]($memberPathBytes.Length + 1))
            $writer.Write($memberPathBytes)
            $writer.Write([byte]0)
            $writer.Write([uint32]$memberBytes.Length)
            $writer.Write($memberBytes)
            $writer.Flush()
        } finally {
            $writer.Dispose()
        }
        $stream.Flush($true)
    } finally {
        $stream.Dispose()
    }

    $listPath = Join-Path $fixtureRoot 'receive-list.txt'
    [System.IO.File]::WriteAllText($listPath,
        "social/temporary-recovery/tri_weapon_recovery.pdca|recovery_tri_weapon|weapon|1`r`n",
        [System.Text.UTF8Encoding]::new($false))
    Write-Host '  generated temporary recovery PDCA fixture' -ForegroundColor DarkGray
}
