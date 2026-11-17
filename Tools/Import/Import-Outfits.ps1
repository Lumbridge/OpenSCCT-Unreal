<#
.SYNOPSIS
Imports the outfit models maps ship (render/outfits.json "model": {"glb"}) to Content\Imported\Outfits (never
committed), headless: no editor window.

.EXAMPLE
Tools\Import\Import-Outfits.ps1 -Content C:\path\to\.content
Tools\Import\Import-Outfits.ps1 -Content C:\path\to\.content -Maps AquaE
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    # Only these map folders (default: every map with an outfits.json).
    [string[]] $Maps = @(),
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
. "$PSScriptRoot\..\EditorScript.ps1"
$script = "$(Fwd "$PSScriptRoot\import_outfits.py") $(Fwd $Content)"
if ($Maps.Count -gt 0) { $script += ' ' + ($Maps -join ' ') }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'import-outfits' -Script $script | Out-Null
Exit-ScctScripts
