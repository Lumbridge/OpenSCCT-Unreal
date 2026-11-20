<#
.SYNOPSIS
Applies processed lightmaps to an independently copied Enhanced Mount Hospital map.

.EXAMPLE
Tools\Import\Import-HospitalLightmaps.ps1 -Manifest C:\assets\lightmap-manifest.json

.EXAMPLE
Tools\Import\Import-HospitalLightmaps.ps1 -Restore
#>
[CmdletBinding(DefaultParameterSetName = 'Apply')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Apply')] [string] $Manifest,
    [Parameter(Mandatory, ParameterSetName = 'Restore')] [switch] $Restore,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$map = Get-Item -LiteralPath (Join-Path $project 'Content\Imported\Maps\MounE')
$folders = @($map, $map.Parent, $map.Parent.Parent, $map.Parent.Parent.Parent)
$linked = @($folders | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
$linked += @(Get-ChildItem -LiteralPath $map.FullName -Recurse -Attributes ReparsePoint)
if ($linked) {
    throw "Make an independent copy of the imported Hospital map before changing its lightmaps: $($linked.FullName -join ', ')"
}

. "$PSScriptRoot\..\EditorScript.ps1"
$mode = if ($Restore) { 'restore' } else { 'apply' }
$arguments = if ($Restore) { '--restore' } else {
    $plan = Fwd (Resolve-Path -LiteralPath $Manifest).Path
    if ($plan -match '\s') { $plan = '"' + $plan + '"' }
    "--manifest $plan"
}
$scriptPath = Fwd "$PSScriptRoot\apply_hospital_lightmaps.py"
if ($scriptPath -match '\s') { $scriptPath = '"' + $scriptPath + '"' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "$mode-hospital-lightmaps" -Script "$scriptPath $arguments" | Out-Null
Exit-ScctScripts
