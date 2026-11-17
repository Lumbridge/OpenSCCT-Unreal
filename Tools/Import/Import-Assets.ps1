<#
.SYNOPSIS
Downloads the CC0 assets of Tools\Import\pbr_sets.json, imports them and rebuilds the master materials
(headless: no editor window). Needed once per checkout only if Content\OpenSCCT is missing them, and again after a
change to pbr_sets.json or scct_materials.py. Re-import the maps afterwards (Import-Map.ps1).

.EXAMPLE
Tools\Import\Import-Assets.ps1                 # everything
Tools\Import\Import-Assets.ps1 -MaterialsOnly  # only rebuild M_ScctSurface, M_ScctGlass, M_ScctWater, M_ScctSky
#>
param(
    [switch] $MaterialsOnly,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$packed = Join-Path $project 'Saved\ScctImport\ThirdParty'

if (-not $MaterialsOnly) {
    # -I: the downloads are data; nothing beside them may be picked up as Python code.
    python -I -B "$PSScriptRoot\fetch_assets.py" (Join-Path $project 'Saved\Downloads') $packed
    if ($LASTEXITCODE -ne 0) { throw 'Fetching the assets failed.' }
}

. "$PSScriptRoot\..\EditorScript.ps1"
$script = "$(Fwd "$PSScriptRoot\import_assets.py") $(Fwd $packed)"
if ($MaterialsOnly) { $script += ' materials' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'import-assets' -Script $script | Out-Null
Exit-ScctScripts
