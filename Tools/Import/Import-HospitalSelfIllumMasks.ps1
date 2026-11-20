<#
.SYNOPSIS
Prepares and applies coverage-preserving glow masks in an independently copied Hospital map.
Run Export-HospitalTextureInventory.ps1 first to capture the effective original textures.

.EXAMPLE
Tools\Import\Import-HospitalSelfIllumMasks.ps1 -PrepareOnly

.EXAMPLE
Tools\Import\Import-HospitalSelfIllumMasks.ps1 -Manifest C:\stage\OriginalRestoration\GlowMasks\self-illum-masks.json

.EXAMPLE
Tools\Import\Import-HospitalSelfIllumMasks.ps1 -Restore
#>
[CmdletBinding(DefaultParameterSetName = 'Apply')]
param(
    [Parameter(ParameterSetName = 'Apply')] [switch] $PrepareOnly,
    [Parameter(ParameterSetName = 'Apply')] [string] $Manifest,
    [Parameter(Mandatory, ParameterSetName = 'Restore')] [switch] $Restore,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$output = Join-Path $project 'Saved\TextureRemasterQA\MounEGlowMasks'
$plan = Join-Path $output 'self-illum-masks.json'
if ($Manifest) {
    $plan = (Resolve-Path -LiteralPath $Manifest).Path
    if ($PrepareOnly) { return }
} elseif (-not $Restore -and ($PrepareOnly -or -not (Test-Path -LiteralPath $plan -PathType Leaf))) {
    $inventory = Join-Path $project 'Saved\TextureRemasterQA\MounETextureInventory\hospital-textures.json'
    python -B "$PSScriptRoot\prepare_hospital_self_illum_masks.py" $inventory $output
    if ($LASTEXITCODE -ne 0) { throw 'Hospital glow-mask preparation failed.' }
    if ($PrepareOnly) { return }
}

$map = Get-Item -LiteralPath (Join-Path $project 'Content\Imported\Maps\MounE')
$folders = @($map, $map.Parent, $map.Parent.Parent, $map.Parent.Parent.Parent)
$linked = @($folders | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
$linked += @(Get-ChildItem -LiteralPath $map.FullName -Recurse -Attributes ReparsePoint)
if ($linked) {
    throw "Make an independent copy of the imported Hospital map before changing its glow masks: $($linked.FullName -join ', ')"
}

. "$PSScriptRoot\..\EditorScript.ps1"
$mode = if ($Restore) { 'restore' } else { 'apply' }
$arguments = if ($Restore) { ' --restore' } else { '' }
$scriptPath = Fwd "$PSScriptRoot\apply_hospital_self_illum_masks.py"
$manifest = Fwd $plan
if ($scriptPath -match '\s') { $scriptPath = '"' + $scriptPath + '"' }
if ($manifest -match '\s') { $manifest = '"' + $manifest + '"' }
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "$mode-hospital-glow-masks" -Script "$scriptPath $manifest$arguments" | Out-Null
Exit-ScctScripts
