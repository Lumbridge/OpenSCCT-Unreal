<#
.SYNOPSIS
Makes the TestBox fixture level /Game/OpenSCCT/Test/TestBox (headless: no editor window).

.DESCRIPTION
TestBox is OpenSCCT's synthetic test map (svmbuild.testbox): a walled box with rooms, a sliding door and four player
starts, nothing of the original game. Tools\Test\make_testbox.py draws the level from the fixture's render world;
the bridge ships the fixture's simulation files in Binaries\Bridge\maps\TestBox (built with the bridge), which is
also where this script takes the fixture from unless -Fixture names another folder. The level is in Git, so this is
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
if (-not $Fixture) {
    $Fixture = Join-Path $project 'Binaries\Bridge\maps\TestBox'
    if (-not (Test-Path (Join-Path $Fixture 'render\world.glb'))) {
        $Fixture = Join-Path $project '..\OSC-unreal-bridge\fixtures\testbox'
    }
}
if (-not (Test-Path (Join-Path $Fixture 'render\world.glb'))) {
    throw "No TestBox fixture at $Fixture (build the bridge, or pass -Fixture)."
}
$Fixture = (Resolve-Path $Fixture).Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'make-testbox' -Script "$(Fwd "$PSScriptRoot\make_testbox.py") $(Fwd $Fixture)" | Out-Null
Exit-ScctScripts
