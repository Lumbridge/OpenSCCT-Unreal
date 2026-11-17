<#
.SYNOPSIS
Imports the fixture maps the checks and drills run on (headless: no editor window), so each one is regenerated from
the converted content rather than made by hand.

.DESCRIPTION
A fixture is a map OpenSCCT generates itself, not one of the original's: TestRange (the practice range of the
Tutorials drills: the vision-cancelling tower with its return portal, the spy-grab lane). Each fixture named is
imported with Tools\Import\Import-Map.ps1 into /Game/Imported/Maps/<Name>/<Name>, as any other map; a fixture whose
converted folder is missing from the content is reported and the script fails. Content/Imported is not in Git, so
run this once per checkout (and again after the converter changes a fixture).

.EXAMPLE
Tools\Test\Import-Fixtures.ps1 -Content C:\path\to\.content

.EXAMPLE
Tools\Test\Import-Fixtures.ps1 -Content C:\path\to\.content -Fixtures TestRange
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    # Which fixtures to import (default: all of them).
    [string[]] $Fixtures = @('TestRange'),
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$importMap = Join-Path $project 'Tools\Import\Import-Map.ps1'

$failed = @()
foreach ($fixture in $Fixtures) {
    $render = Join-Path $Content "svmap\$fixture\render"
    if (-not (Test-Path $render)) {
        Write-Host "$fixture FAILED: the content has no converted fixture at $render"
        $failed += $fixture
        continue
    }

    Write-Host "Importing fixture $fixture"
    # Import-Map ends its own process scope with exit; run it as a child so each fixture reports its own result.
    & pwsh -NoProfile -File $importMap -Map $fixture -Content $Content -Engine $Engine
    $level = Join-Path $project "Content\Imported\Maps\$fixture\$fixture.umap"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $level)) {
        Write-Host "$fixture FAILED: the import did not finish or left no level at $level"
        $failed += $fixture
    }
    else {
        Write-Host "$fixture imported: /Game/Imported/Maps/$fixture/$fixture"
    }
}

if ($failed.Count -gt 0) {
    Write-Host "FAILED: $($failed -join ', ')"
    exit 1
}
exit 0
