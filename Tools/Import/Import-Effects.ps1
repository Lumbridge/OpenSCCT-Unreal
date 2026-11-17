<#
.SYNOPSIS
Rebuilds the effect textures, materials and Niagara systems of Content\OpenSCCT\Effects (headless: no editor
window). Needed only after a change to Tools\Import\scct_effects.py, scct_niagara.py or make_effect_textures.py.
Re-import the maps afterwards.

.EXAMPLE
Tools\Import\Import-Effects.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$textures = Join-Path $project 'Saved\ScctImport\Effects'

python -B "$PSScriptRoot\make_effect_textures.py" $textures
if ($LASTEXITCODE -ne 0) { throw 'Drawing the effect textures failed.' }

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'import-effects' -Script "$(Fwd "$PSScriptRoot\import_effects.py") $(Fwd $textures)" | Out-Null
Exit-ScctScripts
