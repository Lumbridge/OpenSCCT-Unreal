<#
.SYNOPSIS
Rebuilds the demo X-ray outline material headless: no editor window.

.DESCRIPTION
Runs Tools\Materials\build_demo_xray_material.py in the editor's Python and writes
Content\OpenSCCT\Demo\M_ScctDemoXRay.uasset. Run it after changing the shader text in that script.

.EXAMPLE
Tools\Materials\Build-DemoXRayMaterial.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-demo-xray-material' -Script (Fwd "$PSScriptRoot\build_demo_xray_material.py") -Show 'SCCT-XRAY|Error:.*M_ScctDemoXRay' | Out-Null
Exit-ScctScripts
