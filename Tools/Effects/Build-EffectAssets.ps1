<#
.SYNOPSIS
Builds the materials of the match effects (Content\OpenSCCT\Effects\M_ScctFx*) and imports the game's effect
meshes to Content\Imported\Fx (never committed), headless: no editor window.

.EXAMPLE
Tools\Effects\Build-EffectAssets.ps1 -Content C:\path\to\.content
Tools\Effects\Build-EffectAssets.ps1 -Content C:\path\to\.content -Force   # rebuild materials that exist
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    [switch] $Force,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$PSScriptRoot\..\EditorScript.ps1"
$script = Fwd "$PSScriptRoot\build_effect_assets.py"
if ($Content) { $script += " $(Fwd $Content)" }
if ($Force) { $script += ' --force' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-effect-assets' -Script $script | Out-Null
Exit-ScctScripts
