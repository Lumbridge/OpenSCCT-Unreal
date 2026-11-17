<#
.SYNOPSIS
Builds the materials of the level's gameplay markers (Content\OpenSCCT\Level\M_ScctBeam, M_ScctMarker), headless:
no editor window. An existing material is rebuilt only with -Force.

.EXAMPLE
Tools\Level\Build-LevelAssets.ps1
#>
param(
    [switch] $Force,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$PSScriptRoot\..\EditorScript.ps1"
$script = Fwd "$PSScriptRoot\build_level_assets.py"
if ($Force) { $script += ' --force' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-level-assets' -Script $script | Out-Null
Exit-ScctScripts
