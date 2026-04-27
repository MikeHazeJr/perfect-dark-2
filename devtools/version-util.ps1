# version-util.ps1 -- Dot-source only. Semantic version helpers for CMakeLists + git tags.
# Used by build-headless.ps1, release.ps1, and cursor-build.ps1.

Set-StrictMode -Version Latest

function Get-CMakeListsSemVer {
    param([Parameter(Mandatory)][string]$ProjectRoot)
    $path = Join-Path $ProjectRoot "CMakeLists.txt"
    $major = 0; $minor = 0; $patch = 0
    if (-not (Test-Path $path)) {
        return [pscustomobject]@{
            Major = $major; Minor = $minor; Patch = $patch
            String = "0.0.0"
        }
    }
    $c = Get-Content $path -Raw -ErrorAction SilentlyContinue
    if ($c -match 'VERSION_SEM_MAJOR\s+(\d+)') { $major = [int]$Matches[1] }
    if ($c -match 'VERSION_SEM_MINOR\s+(\d+)') { $minor = [int]$Matches[1] }
    if ($c -match 'VERSION_SEM_PATCH\s+(\d+)') { $patch = [int]$Matches[1] }
    return [pscustomobject]@{
        Major = $major; Minor = $minor; Patch = $patch
        String = "$major.$minor.$patch"
    }
}

function Get-GitSemVerTagTriples {
    param([Parameter(Mandatory)][string]$ProjectRoot)
    $list = [System.Collections.Generic.List[object]]::new()
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    $tags = & git -C $ProjectRoot tag -l "v*" 2>$null
    $ErrorActionPreference = $prevEap
    if (-not $tags) { return $list }
    foreach ($t in $tags) {
        if ($t -match '^v(\d+)\.(\d+)\.(\d+)$') {
            $list.Add([pscustomobject]@{
                Major = [int]$Matches[1]; Minor = [int]$Matches[2]; Patch = [int]$Matches[3]; Tag = $t
            }) | Out-Null
        }
    }
    return $list
}

function Test-SemVerGt {
    param(
        [Parameter(Mandatory)]$A,
        [Parameter(Mandatory)]$B
    )
    if ($A.Major -ne $B.Major) { return $A.Major -gt $B.Major }
    if ($A.Minor -ne $B.Minor) { return $A.Minor -gt $B.Minor }
    return $A.Patch -gt $B.Patch
}

function Get-MaxSemVer {
    param([Parameter(Mandatory)][object[]]$Candidates)
    if ($Candidates.Count -eq 0) {
        return [pscustomobject]@{ Major = 0; Minor = 0; Patch = 0 }
    }
    $max = $Candidates[0]
    foreach ($x in $Candidates | Select-Object -Skip 1) {
        if (Test-SemVerGt $x $max) { $max = $x }
    }
    return $max
}

<#
.SYNOPSIS
    Max(VERSION in CMakeLists, all vX.Y.Z git tags), then patch + 1.
.DESCRIPTION
    Does not run git fetch; local tags only. Call git fetch --tags first if needed.
#>
function Get-NextReleaseSemVer {
    param([Parameter(Mandatory)][string]$ProjectRoot)
    $cmake = Get-CMakeListsSemVer $ProjectRoot
    $cmakeObj = [pscustomobject]@{ Major = $cmake.Major; Minor = $cmake.Minor; Patch = $cmake.Patch }
    $tagObjs = Get-GitSemVerTagTriples $ProjectRoot
    $all = [System.Collections.Generic.List[object]]::new()
    $all.Add($cmakeObj) | Out-Null
    foreach ($t in $tagObjs) {
        $all.Add([pscustomobject]@{ Major = $t.Major; Minor = $t.Minor; Patch = $t.Patch }) | Out-Null
    }
    $max = Get-MaxSemVer $all.ToArray()
    $next = [pscustomobject]@{
        Major = $max.Major
        Minor = $max.Minor
        Patch = $max.Patch + 1
    }
    return [pscustomobject]@{
        Previous = $max
        Next     = $next
        NextString = "$($next.Major).$($next.Minor).$($next.Patch)"
    }
}

function Set-CMakeListsSemVer {
    param(
        [Parameter(Mandatory)][string]$ProjectRoot,
        [Parameter(Mandatory)][int]$Major,
        [Parameter(Mandatory)][int]$Minor,
        [Parameter(Mandatory)][int]$Patch
    )
    # Skip the write when content unchanged AND use a no-BOM UTF-8 encoder
    # when we do write. PowerShell 5.1's `Set-Content -Encoding UTF8` always
    # emits a BOM regardless of -NoNewline; that BOM byte made every release
    # leave CMakeLists.txt dirty in the working tree (see S477).
    $path = Join-Path $ProjectRoot "CMakeLists.txt"
    if (-not (Test-Path $path)) { throw "CMakeLists.txt not found: $path" }
    $orig = Get-Content $path -Raw -ErrorAction Stop
    $cmake = $orig -replace '(VERSION_SEM_MAJOR\s+)\d+', ("`${1}" + $Major)
    $cmake = $cmake -replace '(VERSION_SEM_MINOR\s+)\d+', ("`${1}" + $Minor)
    $cmake = $cmake -replace '(VERSION_SEM_PATCH\s+)\d+', ("`${1}" + $Patch)
    if ($cmake -eq $orig) { return }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($path, $cmake, $utf8NoBom)
}
