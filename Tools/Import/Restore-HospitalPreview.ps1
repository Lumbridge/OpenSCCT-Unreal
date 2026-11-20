<#
.SYNOPSIS
Reapply the reviewed Enhanced Mount Hospital preview after normal map import.
.EXAMPLE
Tools/Import/Restore-HospitalPreview.ps1 -Stage Saved/HospitalPreview -PrepareOnly
.EXAMPLE
Tools/Import/Restore-HospitalPreview.ps1 -Stage Saved/HospitalPreview -ValidateOnly
#>
[CmdletBinding(DefaultParameterSetName = 'Apply')]
param(
    [Parameter(Mandatory)] [string] $Stage,
    [string] $Content = $env:SVM_CONTENT,
    [string] $Weights,
    [string] $NeuralPython,
    [int] $Threads = 3,
    [Parameter(Mandatory, ParameterSetName = 'Prepare')] [switch] $PrepareOnly,
    [Parameter(Mandatory, ParameterSetName = 'Validate')] [switch] $ValidateOnly,
    [string] $Engine = 'F:/UE_5.8'
)

$ErrorActionPreference = 'Stop'
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
$stockTextures = Join-Path $Content 'svmap/MounE/render/textures'
if (-not (Test-Path -LiteralPath $stockTextures -PathType Container)) { throw "No converted Mount Hospital textures at $stockTextures." }
$project = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
$stagePath = [IO.Path]::GetFullPath($Stage, (Get-Location).Path)
$projectContent = Join-Path $project 'Content'
if ($stagePath -eq $projectContent -or $stagePath.StartsWith("$projectContent\", [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Stage restoration files outside the project Content directory.'
}
$arguments = @($Content, $stagePath, '--threads', $Threads)
if ($Weights) { $arguments += @('--weights', $Weights) }
if ($NeuralPython) { $arguments += @('--neural-python', $NeuralPython) }
if ($ValidateOnly) { $arguments += '--validate-only' }
python -B "$PSScriptRoot/prepare_hospital_preview.py" @arguments
if ($LASTEXITCODE -ne 0) { throw 'Mount Hospital preview input validation failed.' }
if ($PrepareOnly) { return }

$map = Get-Item -LiteralPath (Join-Path $projectContent 'Imported/Maps/MounE')
if (-not (Test-Path -LiteralPath (Join-Path $map.FullName 'MounE.umap') -PathType Leaf)) {
    throw 'Import MounE with Import-Map.ps1 before applying the preview.'
}
$folders = @($map, $map.Parent, $map.Parent.Parent, $map.Parent.Parent.Parent)
$linked = @($folders | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
$linked += @(Get-ChildItem -LiteralPath $map.FullName -Recurse -Attributes ReparsePoint)
if ($linked.Count) {
    throw "Use a private imported map directory before applying the preview: $($linked.FullName -join ', ')"
}
if ($ValidateOnly) {
    Write-Host "Validated private map package at $($map.FullName)."
    return
}

$openProject = @(Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%' OR Name LIKE 'OpenSCCTUnreal%'" |
    Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').Contains($project, [StringComparison]::OrdinalIgnoreCase) })
if ($openProject.Count) {
    throw "Close the candidate project before reapplying its map assets (PIDs $($openProject.ProcessId -join ', '))."
}
$stockTextures = (Resolve-Path -LiteralPath $stockTextures).Path
$backup = Join-Path $stagePath ('MapBackup-' + [Guid]::NewGuid().ToString('N'))
Copy-Item -LiteralPath $map.FullName -Destination $backup -Recurse
Write-Host "Saved a copy of the imported map at $backup."

$rollback = Join-Path $stagePath 'Colours/base-color-rollback.json'
$previous = Join-Path $project 'Saved/TextureRemasterQA/MounEBaseColor/base-color-rollback.json'
if (-not (Test-Path -LiteralPath $rollback) -and (Test-Path -LiteralPath $previous)) {
    Copy-Item -LiteralPath $previous -Destination $rollback
}

. "$PSScriptRoot/../EditorScript.ps1"
$editorArguments = foreach ($path in @("$PSScriptRoot/apply_hospital_preview.py", $stagePath, $stockTextures)) {
    $argument = Fwd $path
    if ($argument -match '\s') { $argument = '"' + $argument + '"' }
    $argument
}
Invoke-ScctEditorScript -Project $project -Engine $Engine -Name 'restore-hospital-preview' -Script ($editorArguments -join ' ') | Out-Null
Exit-ScctScripts
