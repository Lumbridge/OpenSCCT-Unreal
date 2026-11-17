<#
.SYNOPSIS
Rebuilds the state skins drawn over a pawn's body (the sticky shocker's Stickess skin, the merc's shock-protection
edge, the wet skins) headless: no editor window.

.DESCRIPTION
Runs Tools\Materials\build_pawn_skin_materials.py in the editor's Python. It imports the skins' textures from the
converted content (shared\chars\SPerso\textures) and writes Content\OpenSCCT\Materials\PawnSkins: M_ScctStickySkin,
M_ScctShockImmunityEdge, M_ScctWetSkin and its instances MI_ScctWetSkin_ATT, _ATT2 and _DEF.
Source\OpenSCCT\Play\ScctPawnSkins draws them. Run it after changing that script.

.EXAMPLE
Tools\Materials\Build-PawnSkinMaterials.ps1 -Content C:\path\to\.content
#>
param(
    [Parameter(Mandatory)] [string] $Content,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$contentRoot = (Resolve-Path $Content).Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-pawn-skin-materials' -Script ((Fwd "$PSScriptRoot\build_pawn_skin_materials.py") + ' ' + (Fwd $contentRoot)) -Show 'SCCT-SKINS|SCCT-IMPORT|Error:.*(PawnSkins|M_Scct(StickySkin|ShockImmunityEdge|WetSkin))' | Out-Null
Exit-ScctScripts
