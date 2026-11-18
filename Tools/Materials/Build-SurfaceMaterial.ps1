<#
.SYNOPSIS
Rebuilds the surface master material (M_ScctSurface) and, when it is missing, the lighting plan's parameter
collection (MPC_ScctLight), headless: no editor window.

.DESCRIPTION
Runs Tools\Materials\build_surface_material.py in the editor's Python and writes
Content\OpenSCCT\Materials\M_ScctSurface.uasset from Tools\Import\scct_materials.py. Run it after changing the
surface material there. The glass, water and sky masters are not touched, and no map needs importing again.

.EXAMPLE
Tools\Materials\Build-SurfaceMaterial.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-surface-material' -Script (Fwd "$PSScriptRoot\build_surface_material.py") -Show 'SCCT-IMPORT|Error:.*M_ScctSurface|Error:.*MPC_ScctLight' | Out-Null
Exit-ScctScripts
