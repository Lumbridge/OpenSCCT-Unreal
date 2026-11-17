<#
.SYNOPSIS
Builds the front end level (/Game/OpenSCCT/Maps/L_FrontEnd) in a headless editor: no window.

.DESCRIPTION
The level is the project's start map. It is small and made entirely by build_front_end.py, so it can be rebuilt
at any time; the menu itself is C++ (Source/OpenSCCT/Menu). Build the C++ first: the level names its game mode.

.EXAMPLE
Tools\Menu\Build-FrontEnd.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

# A level that exists cannot be replaced from inside the editor's Python: it is rebuilt from nothing.
Remove-Item (Join-Path $project 'Content\OpenSCCT\Maps\L_FrontEnd.umap') -ErrorAction SilentlyContinue

. "$PSScriptRoot\..\EditorScript.ps1"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'build-front-end' -Script (Fwd "$PSScriptRoot\build_front_end.py") -Show 'SCCT-FRONTEND' | Out-Null
Exit-ScctScripts
