<#
.SYNOPSIS
Exports the copied Mount Hospital map's effective material texture bindings and their source images.

.EXAMPLE
Tools\Import\Export-HospitalTextureInventory.ps1 -ManifestOnly
#>
[CmdletBinding()]
param(
    [switch] $ManifestOnly,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
. "$PSScriptRoot\..\EditorScript.ps1"
$folder = Fwd (Join-Path $project 'Saved\TextureRemasterQA\MounETextureInventory')
$scriptPath = Fwd "$PSScriptRoot\export_hospital_texture_inventory.py"
if ($folder -match '\s') { $folder = '"' + $folder + '"' }
if ($scriptPath -match '\s') { $scriptPath = '"' + $scriptPath + '"' }
$arguments = if ($ManifestOnly) { ' --manifest-only' } else { '' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'export-hospital-textures' -Script "$scriptPath $folder$arguments" | Out-Null
Exit-ScctScripts
