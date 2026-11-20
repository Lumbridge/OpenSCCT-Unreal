<#
.SYNOPSIS
Binds uncompressed source sprite and corona textures in an independently copied Enhanced Mount Hospital map.

.EXAMPLE
Tools\Import\Import-HospitalEffectTextures.ps1

.EXAMPLE
Tools\Import\Import-HospitalEffectTextures.ps1 -Restore
#>
[CmdletBinding()]
param(
    [string] $Content = "$env:LOCALAPPDATA\OpenSCCT\packs",
    [switch] $Restore,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$map = Get-Item -LiteralPath (Join-Path $project 'Content\Imported\Maps\MounE')
$folders = @($map, $map.Parent, $map.Parent.Parent, $map.Parent.Parent.Parent)
$linked = @($folders | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
$linked += @(Get-ChildItem -LiteralPath $map.FullName -Recurse -Attributes ReparsePoint)
if ($linked) {
    throw "Make an independent copy of the imported Hospital map before changing its effect textures: $($linked.FullName -join ', ')"
}

. "$PSScriptRoot\..\EditorScript.ps1"
$mode = if ($Restore) { 'restore' } else { 'apply' }
$arguments = if ($Restore) { '--restore' } else {
    $textures = Fwd (Resolve-Path -LiteralPath (Join-Path $Content 'svmap\MounE\render\textures')).Path
    if ($textures -match '\s') { $textures = '"' + $textures + '"' }
    "--textures $textures"
}
$scriptPath = Fwd "$PSScriptRoot\apply_hospital_effect_textures.py"
if ($scriptPath -match '\s') { $scriptPath = '"' + $scriptPath + '"' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "$mode-hospital-effect-textures" -Script "$scriptPath $arguments" | Out-Null
Exit-ScctScripts
