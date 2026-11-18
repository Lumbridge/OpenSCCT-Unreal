<#
.SYNOPSIS
Rebuilds the poison overlay's brightening material headless: no editor window.

.DESCRIPTION
Runs Tools\Materials\build_poison_material.py in the editor's Python and writes
Content\OpenSCCT\UI\Vision\M_ScctPoisonDodge.uasset. Run it after changing the shader text in that script.

.EXAMPLE
Tools\Materials\Build-PoisonMaterial.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-poison-material' -Script (Fwd "$PSScriptRoot\build_poison_material.py") -Show 'SCCT-POISON|Error:.*M_ScctPoisonDodge' | Out-Null
Exit-ScctScripts
