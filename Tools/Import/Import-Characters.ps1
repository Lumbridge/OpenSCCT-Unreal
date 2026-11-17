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
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    # Make the equipment materials again from the script (they are otherwise kept as they are).
    [switch] $RebuildMaterials,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
if (-not (Test-Path (Join-Path $Content 'shared\chars'))) { throw "No converted characters at $Content\shared\chars." }

# Unreal's command line treats backslashes in -script as escapes.
function Fwd([string] $path) { $path -replace '\\', '/' }

function Invoke-EditorScript([string] $name, [string] $script) {
    $log = Fwd (Join-Path $project "Saved\Logs\$name.log")
    & "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Fwd "$project\OpenSCCTUnreal.uproject") -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi "-abslog=$log" *> $null
    $code = $LASTEXITCODE
    Select-String -Path $log -CaseSensitive -Pattern 'SCCT-IMPORT|Traceback|LogPython: Error' |
        ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -Unique
    "$name exit code $code (log: $log)"
}

$rebuild = if ($RebuildMaterials) { ' rebuild' } else { '' }
Invoke-EditorScript 'build-equipment' "$(Fwd "$PSScriptRoot\build_equipment_assets.py")$rebuild"
Invoke-EditorScript 'import-characters' "$(Fwd "$PSScriptRoot\import_characters.py") $(Fwd $Content)"
