<#
.SYNOPSIS
Rebuilds the goggles' post-process materials (night, thermal, motion tracking, EMF) headless: no editor window.

.DESCRIPTION
Runs Tools\Materials\build_vision_materials.py in the editor's Python and writes
Content\OpenSCCT\UI\Vision\M_ScctVision*.uasset. Run it after changing the shader text in that script.

.EXAMPLE
Tools\Materials\Build-VisionMaterials.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-vision-materials' -Script (Fwd "$PSScriptRoot\build_vision_materials.py") -Show 'SCCT-VISION|Error:.*M_ScctVision' | Out-Null
Exit-ScctScripts
