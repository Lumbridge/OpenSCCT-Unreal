<#
.SYNOPSIS
Opens the menus off-screen (no window, never takes focus), follows a menu script and quits.

.DESCRIPTION
Starts the front end level (or a map, for the pause menu) with -RenderOffscreen and passes -ScctMenuDo, the menu
driver's script (Source/OpenSCCT/Menu/ScctMenuDriver.h): "page play; key Down 2; press START MATCH; shot play; quit".
Screenshots go to Saved\ScctShots\Menu (or -ShotDir), the log to Saved\Logs\menu<Tag>.log.
Run one at a time: several off-screen runs of one checkout at once can hang.

.EXAMPLE
Tools\Menu\Run-MenuShots.ps1 -Do "page home; wait 2; shot home; quit"

.EXAMPLE
Tools\Menu\Run-MenuShots.ps1 -ResX 1280 -ResY 720 -Tag 720 -Do "page settings/bindings; wait 1; shot bindings_720; quit"
#>
param(
    [Parameter(Mandatory)] [string] $Do,
    [int] $ResX = 1920,
    [int] $ResY = 1080,
    [string] $Tag = '',
    [string] $ShotDir = '',
    # The level to open: the front end by default, "/Game/Imported/Maps/AquaE/AquaE?Side=merc" for a match.
    [string] $Level = '/Game/OpenSCCT/Maps/L_FrontEnd',
    [string] $Content = $(if ($env:SVM_CONTENT) { $env:SVM_CONTENT } else { 'C:\Users\ryans\source\repos\SVM-Rewrite\.content' }),
    [string] $Extra = '',
    [int] $TimeoutSeconds = 300,
    [string] $Engine = 'F:\UE_5.8'
)

function Format-Switch([string] $Key, [string] $Value) {
    if ($Value.Contains('"')) { throw "The value of -$Key must not contain a double quote: $Value" }
    "-$Key=`"$Value`""
}

$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $ShotDir) { $ShotDir = Join-Path $project 'Saved\ScctShots\Menu' }
New-Item -ItemType Directory -Force $ShotDir | Out-Null
New-Item -ItemType Directory -Force (Join-Path $project 'Saved\Logs') | Out-Null
$log = (Join-Path $project "Saved\Logs\menu$Tag.log").Replace('\', '/')

$arguments = @(
    "`"$project\OpenSCCTUnreal.uproject`"",
    "`"$Level`"",
    '-game', '-RenderOffscreen', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes', '-unattended', '-nosplash', '-nosound',
    (Format-Switch 'ScctContent' $Content.Replace('\', '/')),
    (Format-Switch 'ScctShotDir' $ShotDir.Replace('\', '/')),
    (Format-Switch 'ScctMenuDo' $Do),
    (Format-Switch 'abslog' $log),
    $Extra
) -join ' '

$run = Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $arguments `
    -NoNewWindow -PassThru `
    -RedirectStandardOutput (Join-Path $project "Saved\Logs\menu$Tag.stdout.txt") `
    -RedirectStandardError (Join-Path $project "Saved\Logs\menu$Tag.stderr.txt")
if (-not $run.WaitForExit($TimeoutSeconds * 1000)) {
    # Only the process this script started is stopped.
    Stop-Process -Id $run.Id -Force
    "timed out after $TimeoutSeconds s"
}
else { "exit code $($run.ExitCode)" }

Select-String -Path $log -Pattern 'LogOpenSCCT' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -Last 60
