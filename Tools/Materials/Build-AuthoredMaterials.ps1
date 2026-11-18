param([string] $Engine = 'F:\UE_5.8')

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-authored-materials' -Script (Fwd "$PSScriptRoot\build_authored_materials.py") | Out-Null
Exit-ScctScripts
