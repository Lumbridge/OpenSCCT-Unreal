<#
.SYNOPSIS
Imports the characters and everything they hold and wear, and builds the project's own equipment assets
(headless: no editor window).

.DESCRIPTION
Runs build_equipment_assets.py (materials and the weapon table under /Game/OpenSCCT/Weapons, committed) and then
import_characters.py (bodies, weapons, first-person models and goggle lenses under /Game/Imported/Chars, never
committed). Build the game's C++ first: the weapon table is an asset of a class of the game module.

.EXAMPLE
Tools\Import\Import-Characters.ps1 -Content C:\path\to\.content

.EXAMPLE
Tools\Import\Import-Characters.ps1 -Content C:\path\to\.content -RebuildMaterials

.EXAMPLE
Tools\Import\Import-Characters.ps1 -Content "$env:LOCALAPPDATA\OpenSCCT\packs" -RiflesOnly
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    # Make the equipment materials again from the script (they are otherwise kept as they are).
    [switch] $RebuildMaterials,
    # Import only the additional merc rifles with their authored materials.
    [switch] $RiflesOnly,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
if (-not (Test-Path (Join-Path $Content 'shared\chars'))) { throw "No converted characters at $Content\shared\chars." }

. "$PSScriptRoot\..\EditorScript.ps1"
$rebuild = if ($RebuildMaterials) { ' rebuild' } else { '' }
if (-not $RiflesOnly) {
    Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-equipment' -Script "$(Fwd "$PSScriptRoot\build_equipment_assets.py")$rebuild" | Out-Null
}
$selection = if ($RiflesOnly) { ' --rifles-only' } else { '' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'import-characters' -Script "$(Fwd "$PSScriptRoot\import_characters.py") $(Fwd $Content)$selection" | Out-Null
Exit-ScctScripts
