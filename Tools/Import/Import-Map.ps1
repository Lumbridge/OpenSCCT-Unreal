<#
.SYNOPSIS
Imports a converted OpenSCCT map into the Unreal project as a level (headless: no editor window).

.EXAMPLE
Tools\Import\Import-Map.ps1 -Map AquaE -Content C:\path\to\.content
#>
param(
    [Parameter(Mandatory)] [string] $Map,
    [string] $Content = $env:SVM_CONTENT,
    [switch] $PropsOnly,
    [switch] $NativeLighting,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
$render = Join-Path $Content "svmap\$Map\render"
if (-not (Test-Path $render)) { throw "No converted map at $render." }

. "$PSScriptRoot\..\EditorScript.ps1"
if ($PropsOnly) {
    Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "import-props-$Map" -Script "$(Fwd "$PSScriptRoot\import_map.py") $(Fwd $Content) $Map --props-only" | Out-Null
    Exit-ScctScripts
}

$normals = Join-Path $project "Saved\ScctImport\$Map\normals"
python "$PSScriptRoot\prepare_textures.py" $render $normals
if ($LASTEXITCODE -ne 0) { throw 'Texture preparation failed.' }

# What stands in the rain's way in each rain volume (roofs, awnings, trees), for the rain and the wet ground.
$rain = Join-Path $project "Saved\ScctImport\$Map\rain"
python -B "$PSScriptRoot\prepare_rain_cover.py" (Join-Path $Content "svmap\$Map") $rain
if ($LASTEXITCODE -ne 0) { throw 'Working out the rain cover failed.' }

if ($Map -eq 'MounE') {
    $shaft = Join-Path $project "Saved\ScctImport\$Map\SideStairs"
    python -B "$PSScriptRoot\prepare_hospital_side_stairs_beam.py" $render $shaft
    if ($LASTEXITCODE -ne 0) { throw 'Could not reconstruct the original Side Stairs grate.' }
}

# A level that exists cannot be replaced from inside the editor's Python: it is rebuilt from nothing each import.
Remove-Item (Join-Path $project "Content\Imported\Maps\$Map\$Map.umap") -ErrorAction SilentlyContinue

$imported = Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "import-$Map" -Script "$(Fwd "$PSScriptRoot\import_map.py") $(Fwd $Content) $Map $(Fwd $normals) $(Fwd $rain)"
if ($imported -and $Map -eq 'MounE') {
    $manifest = Fwd (Join-Path $shaft 'SideStairsShaft.json')
    $imported = Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "import-$Map-skylight" -Script "$(Fwd "$PSScriptRoot\apply_hospital_side_stairs_beam.py") $manifest --include-floor"
}
if ($imported -and $NativeLighting) {
    & "$PSScriptRoot/Build-MapLighting.ps1" -Map $Map -Content $Content -Engine $Engine
}
Exit-ScctScripts
