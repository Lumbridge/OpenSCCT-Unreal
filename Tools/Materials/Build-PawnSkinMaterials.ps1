<#
.SYNOPSIS
Rebuilds the state skins drawn over a pawn's body (the sticky shocker's Stickess skin, the merc's shock-protection
edge, the wet skins) headless: no editor window.

.DESCRIPTION
Runs Tools\Materials\build_pawn_skin_materials.py in the editor's Python. It imports no texture (the skins' textures are the original
game's: the game makes them at run time from the player's converted content) and writes Content\OpenSCCT\Materials\PawnSkins: M_ScctStickySkin,
M_ScctShockImmunityEdge, M_ScctWetSkin and its instances MI_ScctWetSkin_ATT, _ATT2 and _DEF.
Source\OpenSCCT\Play\ScctPawnSkins draws them. Run it after changing that script.

.EXAMPLE
Tools\Materials\Build-PawnSkinMaterials.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-pawn-skin-materials' -Script ((Fwd "$PSScriptRoot\build_pawn_skin_materials.py")) -Show 'SCCT-SKINS|SCCT-IMPORT|Error:.*(PawnSkins|M_Scct(StickySkin|ShockImmunityEdge|WetSkin))' | Out-Null
Exit-ScctScripts
