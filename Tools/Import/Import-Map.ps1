<#
.SYNOPSIS
Imports a converted OpenSCCT map into the Unreal project as a level (headless: no editor window).

.EXAMPLE
Tools\Import\Import-Map.ps1 -Map AquaE -Content C:\path\to\.content
#>
param(
    [Parameter(Mandatory)] [string] $Map,
    [string] $Content = $env:SVM_CONTENT,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
$render = Join-Path $Content "svmap\$Map\render"
if (-not (Test-Path $render)) { throw "No converted map at $render." }

$normals = Join-Path $project "Saved\ScctImport\$Map\normals"
python "$PSScriptRoot\prepare_textures.py" $render $normals
if ($LASTEXITCODE -ne 0) { throw 'Texture preparation failed.' }

# A level that exists cannot be replaced from inside the editor's Python: it is rebuilt from nothing each import.
Remove-Item (Join-Path $project "Content\Imported\Maps\$Map\$Map.umap") -ErrorAction SilentlyContinue

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "import-$Map" -Script "$(Fwd "$PSScriptRoot\import_map.py") $(Fwd $Content) $Map $(Fwd $normals)" | Out-Null
Exit-ScctScripts
