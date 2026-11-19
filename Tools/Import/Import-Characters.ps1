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

.EXAMPLE
Tools\Import\Import-Characters.ps1 -Content "$env:LOCALAPPDATA\OpenSCCT\packs" -MagnumReloadOnly
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    # Make the equipment materials again from the script (they are otherwise kept as they are).
    [switch] $RebuildMaterials,
    # Import only the additional merc rifles with their authored materials.
    [switch] $RiflesOnly,
    # Refresh only the .44 Magnum first-person model and its six improved cases.
    [switch] $MagnumReloadOnly,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
if (-not (Test-Path (Join-Path $Content 'shared\chars'))) { throw "No converted characters at $Content\shared\chars." }

. "$PSScriptRoot\..\EditorScript.ps1"
if ($MagnumReloadOnly) {
    if ($RiflesOnly -or $RebuildMaterials) { throw '-MagnumReloadOnly cannot be combined with the other import switches.' }
    $source = Join-Path $Content 'shared/chars/OpenScctSidearms/magnum44_first.glb'
    $glb = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $source).Path)
    $jsonSize = [BitConverter]::ToInt32($glb, 12)
    $json = [Text.Encoding]::UTF8.GetString($glb, 20, $jsonSize) | ConvertFrom-Json
    $materials = $json.materials.name
    if ('OpenScctSidearms.CaseBrass' -notin $materials -or 'OpenScctSidearms.CasePrimer' -notin $materials) {
        throw "The converted model is older than the improved Magnum cases: $source. Use the updated shared content pack."
    }

    # Import into the usual package path with no stale sibling meshes or material slots. Keep the entire prior
    # generated folder outside Content so a failed import can restore it and no old case gets cooked beside it.
    $folder = Join-Path $project 'Content\Imported\Chars\OpenScctSidearms\magnum44_first'
    $backup = Join-Path $project ('Saved\CharacterImportBackups\magnum44_first-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    if (Test-Path -LiteralPath $folder) {
        $existing = Get-Item -LiteralPath $folder
        if ($existing.FullName -ne $folder -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Unexpected generated asset directory: $($existing.FullName)"
        }
        Move-Item -LiteralPath $folder -Destination (Join-Path $backup 'previous')
    }
    try {
        $imported = Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'import-magnum-reload' `
            -Script "$(Fwd "$PSScriptRoot\import_characters.py") $(Fwd $Content) --magnum-reload-only"
        if (-not $imported) { throw 'The Magnum reload import failed.' }
    } catch {
        if (Test-Path -LiteralPath $folder) { Move-Item -LiteralPath $folder -Destination (Join-Path $backup 'failed') }
        $previous = Join-Path $backup 'previous'
        if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $folder }
        throw
    }
    Write-Host "Magnum import backup directory: $backup"
    Exit-ScctScripts
}

$rebuild = if ($RebuildMaterials) { ' rebuild' } else { '' }
if (-not $RiflesOnly) {
    Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-equipment' -Script "$(Fwd "$PSScriptRoot\build_equipment_assets.py")$rebuild" | Out-Null
}
$selection = if ($RiflesOnly) { ' --rifles-only' } else { '' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'import-characters' -Script "$(Fwd "$PSScriptRoot\import_characters.py") $(Fwd $Content)$selection" | Out-Null
Exit-ScctScripts
