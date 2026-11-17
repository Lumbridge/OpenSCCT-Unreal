<#
.SYNOPSIS
Plays an imported map off-screen (no window, never takes focus), saves screenshots and quits.

.EXAMPLE
Tools\Run-Offscreen.ps1 -Map AquaE -Content C:\path\to\.content -Shots 3
#>
param(
    [Parameter(Mandatory)] [string] $Map,
    [string] $Content = $env:SVM_CONTENT,
    [int] $Shots = 3,
    [double] $Every = 6,
    [string] $Options = '',
    # Camera tour: "x,y,z,yaw,pitch;x,y,z,yaw,pitch" (one per screenshot) instead of the player's view.
    [string] $Cams = '',
    [string] $Engine = 'F:\UE_5.8'
)

$project = (Resolve-Path "$PSScriptRoot\..").Path
$shotDir = (Join-Path $project 'Saved\ScctShots').Replace('\', '/')
$log = (Join-Path $project "Saved\Logs\run-$Map.log").Replace('\', '/')
$contentDir = $Content.Replace('\', '/')

& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$project\OpenSCCTUnreal.uproject" "/Game/Imported/Maps/$Map/$Map$Options" `
    -game -RenderOffscreen -ResX=1920 -ResY=1080 -unattended -nosplash -nosound `
    "-ScctContent=$contentDir" "-ScctShots=$Shots" "-ScctShotEvery=$Every" "-ScctShotDir=$shotDir" "-ScctCams=$Cams" "-abslog=$log" *> $null
"exit code $LASTEXITCODE"

Select-String -Path $log -Pattern 'LogOpenSCCT' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -First 30
