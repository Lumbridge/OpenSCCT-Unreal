<#
.SYNOPSIS
Applies optional higher-resolution base colours to an independently copied Enhanced Mount Hospital map.
Input JSON maps original PNG filenames to replacement PNG paths relative to that JSON.

.EXAMPLE
Tools\Import\Import-HospitalBaseColors.ps1 -Manifest C:\assets\upscaled.json

.EXAMPLE
Tools\Import\Import-HospitalBaseColors.ps1 -Restore
#>
[CmdletBinding(DefaultParameterSetName = 'Apply')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Apply')] [string] $Manifest,
    [Parameter(ParameterSetName = 'Apply')] [string] $Inventory,
    [Parameter(ParameterSetName = 'Apply')] [string] $Content = "$env:LOCALAPPDATA\OpenSCCT\packs",
    [Parameter(ParameterSetName = 'Apply')] [switch] $PrepareOnly,
    [Parameter(Mandatory, ParameterSetName = 'Restore')] [switch] $Restore,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$work = Join-Path $project 'Saved\TextureRemasterQA\MounEBaseColor'
$plan = Join-Path $work 'base-color-plan.json'
$snapshot = Join-Path $work 'base-color-rollback.json'
if (-not $Restore) {
    if (-not $Inventory) { $Inventory = Join-Path $project 'Saved\TextureRemasterQA\MounE\detail-manifest.json' }
    $originals = Join-Path $Content 'svmap\MounE\render\textures'
    python -B "$PSScriptRoot\prepare_hospital_base_colors.py" $Manifest $Inventory $originals $work
    if ($LASTEXITCODE -ne 0) { throw 'Mount Hospital base colour preparation failed.' }
    if ($PrepareOnly) { return }
}

$map = Get-Item -LiteralPath (Join-Path $project 'Content\Imported\Maps\MounE')
$materials = Get-Item -LiteralPath (Join-Path $map.FullName 'Materials')
$destination = Get-Item -LiteralPath (Join-Path $map.FullName 'HospitalBaseColors') -ErrorAction SilentlyContinue
foreach ($folder in @($map, $materials, $map.Parent, $map.Parent.Parent, $map.Parent.Parent.Parent, $destination)) {
    if ($folder -and ($folder.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Make an independent copy of $($folder.FullName) before modifying the imported Hospital materials."
    }
}
if ($Restore -and -not (Test-Path -LiteralPath $snapshot -PathType Leaf)) {
    Write-Host 'No active Mount Hospital base colour bindings to restore.'
    return
}

. "$PSScriptRoot\..\EditorScript.ps1"
$mode = if ($Restore) { 'restore' } else { 'apply' }
$command = "$(Fwd "$PSScriptRoot\apply_hospital_base_colors.py") $mode $(Fwd $plan) $(Fwd $snapshot)"
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "$mode-hospital-base-colors" -Script $command | Out-Null
Exit-ScctScripts
