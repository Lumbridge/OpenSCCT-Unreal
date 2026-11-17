<#
.SYNOPSIS
Rebuilds Content\OpenSCCT\RuntimeMap (headless: no editor window): the runtime level that draws a converted map the
editor never imported, and its surface material instances. Needed only after a change to make_runtime_map.py, to
the surface or sky material, or to the default sky of map_skies.json.

.EXAMPLE
Tools\Import\Make-RuntimeMap.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

# A level that exists cannot be replaced from inside the editor's Python: it is rebuilt from nothing.
Remove-Item (Join-Path $project 'Content\OpenSCCT\RuntimeMap\L_RuntimeMap.umap') -ErrorAction SilentlyContinue

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'make-runtime-map' -Script "$(Fwd "$PSScriptRoot\make_runtime_map.py")" | Out-Null
Exit-ScctScripts
