<#
.SYNOPSIS
Rebuilds the lighting plan's tone material (the picture's transfer under scct.Light.Plan 1) headless: no editor window.

.DESCRIPTION
Runs Tools\Materials\build_tone_material.py in the editor's Python and writes
Content\OpenSCCT\Materials\M_ScctTone.uasset. Run it after changing the shader text in that script.

.EXAMPLE
Tools\Materials\Build-ToneMaterial.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-tone-material' -Script (Fwd "$PSScriptRoot\build_tone_material.py") -Show 'SCCT-TONE|Error:.*M_ScctTone' | Out-Null
Exit-ScctScripts
