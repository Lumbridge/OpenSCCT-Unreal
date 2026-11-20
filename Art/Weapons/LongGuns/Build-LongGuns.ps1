<#
.SYNOPSIS
Build the four merc gun models, their first-person rigs, and their authored game animations.
.EXAMPLE
.\Build-LongGuns.ps1 -ContentRoot $env:SVM_CONTENT -Install
.NOTES
Use the bundled Runtime/content/svmbuild with its Python requirements and Blender 5.2.
For a fresh Unreal project, run Tools/Import/Import-Characters.ps1 -Content <content root> -RiflesOnly
after installing the exports. This is the same importer used for the other optional merc rifles.
#>
[CmdletBinding()]
param(
    [string]$SvmRoot = "$PSScriptRoot\..\..\..\Runtime",
    [Parameter(Mandatory)][string]$ContentRoot,
    [string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe',
    [string]$Python = 'python',
    [switch]$Install
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$svm = (Resolve-Path -LiteralPath $SvmRoot).Path
$packs = (Resolve-Path -LiteralPath $ContentRoot).Path
$riflePack = Join-Path $packs 'shared\chars\OpenScctRifles'
$template = Join-Path $riflePack 'm4a1s_first.glb'
if (-not (Test-Path -LiteralPath $template -PathType Leaf)) {
    throw "Missing the original OpenSCCT arm animation template at $template."
}
$work = Join-Path $project 'Intermediate\LongGunArt'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$sources = @{
    SMG = 'Scorpion\smg.obj'
    DMR = 'Dragunov\dragunov.blend'
    Bolt = 'LeeEnfield\rifle.fbx'
}
foreach ($entry in $sources.GetEnumerator()) {
    $inputModel = Join-Path "$PSScriptRoot\Sources" $entry.Value
    $intermediate = Join-Path $work ($entry.Key + '.npz')
    & $Blender --background --factory-startup --python-exit-code 1 --python "$PSScriptRoot\read_source_mesh.py" -- $inputModel $intermediate
    if ($LASTEXITCODE -ne 0) { throw "Blender failed to read $inputModel" }
}
& $Python "$PSScriptRoot\build_long_guns.py" $work $svm
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare weapon geometry and materials.' }
& $Python "$PSScriptRoot\make_first_person.py" --content "$svm\content" --template $template
if ($LASTEXITCODE -ne 0) { throw 'Could not build the first-person and external animations.' }
if ($Install) {
    foreach ($destination in @($riflePack, "$svm\content\assets\rifles\export")) {
        & $Python "$PSScriptRoot\install_long_guns.py" $destination
        if ($LASTEXITCODE -ne 0) { throw "Could not install the gun pack in $destination" }
    }
}
