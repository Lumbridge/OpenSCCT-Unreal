<#
.SYNOPSIS
Imports the merc's rifle (Art\Weapons\MercRifle) into the Unreal project, headless: no editor window. With -Look
it then renders the rifle's look level off-screen and saves screenshots to Saved\ScctShots\L_RifleLook_NN.png.

.EXAMPLE
Tools\Import\Import-MercRifle.ps1
Tools\Import\Import-MercRifle.ps1 -Look
Tools\Import\Import-MercRifle.ps1 -Look -SkipImport
#>
param(
    [switch] $Look,
    [switch] $SkipImport,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$source = Join-Path $project 'Art\Weapons\MercRifle'
if (-not (Test-Path "$source\MercRifle.fbx")) { throw "No $source\MercRifle.fbx: run Art\Weapons\MercRifle\Build-MercRifle.ps1 first." }

# Unreal's command line treats backslashes in -script as escapes.
function Fwd([string] $path) { $path -replace '\\', '/' }
$editor = "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

if (-not $SkipImport) {
    # A level that exists cannot be replaced from inside the editor's Python: it is rebuilt from nothing each import.
    Remove-Item (Join-Path $project 'Content\OpenSCCT\Weapons\MercRifle\L_RifleLook.umap') -ErrorAction SilentlyContinue

    $log = Fwd (Join-Path $project 'Saved\Logs\import-MercRifle.log')
    $script = "$(Fwd "$PSScriptRoot\import_merc_rifle.py") $(Fwd $source)"
    & $editor (Fwd "$project\OpenSCCTUnreal.uproject") -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi "-abslog=$log" *> $null
    $code = $LASTEXITCODE
    Select-String -Path $log -CaseSensitive -Pattern 'SCCT-IMPORT|Traceback|LogPython: Error' |
        ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -Unique
    "import exit code $code (log: $log)"
}

if ($Look) {
    # One camera per screenshot: "x,y,z,yaw,pitch". The three rifles stand at y = -90, 0 and 100, 110 cm up:
    # side view, three-quarter, the merc's own view, a close look at the sight and receiver, and all three together.
    $cams = '-55,-98,120,0,-5;-62,22,128,-17.5,-15.7;-42,86,131.5,7.4,-5.9;-25,-92,134,-11,-20;-150,0,160,0,-16'
    $shots = Fwd (Join-Path $project 'Saved\ScctShots')
    $log = Fwd (Join-Path $project 'Saved\Logs\look-MercRifle.log')
    & $editor "$project\OpenSCCTUnreal.uproject" '/Game/OpenSCCT/Weapons/MercRifle/L_RifleLook' `
        -game -RenderOffscreen -ResX=1920 -ResY=1080 -unattended -nosplash -nosound `
        '-ScctShots=5' '-ScctShotEvery=5' "-ScctShotDir=$shots" "-ScctCams=$cams" "-abslog=$log" *> $null
    "look exit code $LASTEXITCODE (shots: $shots\L_RifleLook_NN.png, log: $log)"
    Select-String -Path $log -Pattern 'LogOpenSCCT|Error:' |
        ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -First 20
}
