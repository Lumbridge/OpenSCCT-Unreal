<#
.SYNOPSIS
Makes the TestBox fixture level /Game/OpenSCCT/Test/TestBox (headless: no editor window).

.DESCRIPTION
TestBox is OpenSCCT's synthetic test map (svmbuild.testbox): a walled box with rooms, a sliding door and four player
starts, nothing of the original game. Tools\Test\make_testbox.py draws the level from the fixture's render world;
the source fixture lives in Runtime\fixtures\testbox. The bridge also ships its simulation files in
Binaries\Bridge\maps\TestBox. The level is in Git, so this is
only needed when the fixture changes.

.EXAMPLE
Tools\Test\Make-TestBox.ps1
#>
param(
    # The fixture folder (manifest.json, render\world.glb, sim\...).
    [string] $Fixture = '',
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Fixture) { $Fixture = Join-Path $project 'Runtime\fixtures\testbox' }
if (-not (Test-Path (Join-Path $Fixture 'render\world.glb'))) {
    throw "No TestBox fixture at $Fixture (check Runtime\fixtures or pass -Fixture)."
}
$Fixture = (Resolve-Path $Fixture).Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'make-testbox' -Script "$(Fwd "$PSScriptRoot\make_testbox.py") $(Fwd $Fixture)" | Out-Null
Exit-ScctScripts
