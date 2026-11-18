<#
.SYNOPSIS
Plays an imported map off-screen (no window, never takes focus), saves screenshots and quits.

.DESCRIPTION
Starts the level with a live local match and writes Saved\ScctShots\<Map>_NN.png and Saved\Logs\run-<Map>.log.
Folders and values with spaces are fine: every value goes on the command line as -Key="value", which is the
form Unreal's parser reads whole (a quote around the entire argument would cut the value at its first space).

.EXAMPLE
Tools\Run-Offscreen.ps1 -Map AquaE -Content C:\path\to\.content -Shots 3

.EXAMPLE
Tools\Run-Offscreen.ps1 -Map AquaE -Options "?Side=merc?Bots=3?Difficulty=hard" -Extra "-ScctCheckFeatures"
#>
param(
    [Parameter(Mandatory)] [string] $Map,
    [string] $Content = $env:SVM_CONTENT,
    [int] $Shots = 3,
    [double] $Every = 6,
    # Where the screenshots go (default: Saved\ScctShots in the project).
    [string] $ShotDir = '',
    # Added to the log's name (run-<Map><Tag>.log), so runs of one map do not overwrite each other's log.
    [string] $Tag = '',
    # Game URL options, see Source/OpenSCCT/Play/ScctGameMode.h ("?Side=merc?Bots=3").
    [string] $Options = '',
    # Camera tour: "x,y,z,yaw,pitch;x,y,z,yaw,pitch" (one per screenshot) instead of the player's view.
    [string] $Cams = '',
    # Console commands to run at start, comma separated (look experiments: "r.ExposureOffset 2").
    [string] $Exec = '',
    # Anything else for the command line, as written ("-ScctCheckFeatures -ResX=1280 -ResY=720").
    [string] $Extra = '',
    # Run the audio engine without a sound device (the engine's non-realtime renderer, -DeterministicAudio)
    # instead of no audio engine at all (-nosound): voices really play, into nothing. Never audible.
    [switch] $NullAudio,
    # Run the audio engine on the machine's real output devices (neither -nosound nor -DeterministicAudio): the device
    # list and the spatialization plugin are only there then. An off-screen window has no focus, so the engine plays it
    # at its unfocused volume.
    [switch] $DeviceAudio,
    # Draw the map in the runtime level from its render files even when it has an imported level (a map without one
    # always is: Source/OpenSCCT/Play/ScctRuntimeMapActor.h). Point -Extra "-ScctMaps=<dir>" at maps outside the content.
    [switch] $Runtime,
    [string] $Engine = 'F:\UE_5.8',
    # A level to open instead of the imported map (-Map still names the log): /Game/OpenSCCT/Maps/L_FrontEnd for the menu.
    [string] $Level = ''
)

function Format-Switch([string] $Key, [string] $Value) {
    # Unreal reads -Key="a value" whole; a quote inside the value would end it early, so there must be none.
    if ($Value.Contains('"')) { throw "The value of -$Key must not contain a double quote: $Value" }
    "-$Key=`"$Value`""
}

$project = (Resolve-Path "$PSScriptRoot\..").Path
if (-not $ShotDir) { $ShotDir = Join-Path $project 'Saved\ScctShots' }
$shotDir = $ShotDir.Replace('\', '/')
$log = (Join-Path $project "Saved\Logs\run-$Map$Tag.log").Replace('\', '/')
$contentDir = "$Content".Replace('\', '/')
New-Item -ItemType Directory -Force (Join-Path $project 'Saved\Logs') | Out-Null
# PowerShell names are not case sensitive: $levelPath, not $level, which would be the -Level parameter.
$levelPath = "/Game/Imported/Maps/$Map/$Map"
if ($Level) {
    $levelPath = $Level
} elseif ($Runtime -or -not (Test-Path (Join-Path $project "Content\Imported\Maps\$Map\$Map.umap"))) {
    $levelPath = "/Game/OpenSCCT/RuntimeMap/L_RuntimeMap?ScctMap=$Map"
}

$arguments = @(
    "`"$project\OpenSCCTUnreal.uproject`"",
    "`"$levelPath$Options`"",
    # Unreal reads the first -ResX on the line, so a size given in -Extra replaces the default instead of following it.
    '-game', '-RenderOffscreen', $(if ($Extra -match '-ResX=') { '' } else { '-ResX=1920 -ResY=1080' }), '-ForceRes', '-unattended', '-nosplash',
    $(if ($DeviceAudio) { '' } elseif ($NullAudio) { '-DeterministicAudio' } else { '-nosound' }),
    (Format-Switch 'ScctContent' $contentDir),
    "-ScctShots=$Shots",
    "-ScctShotEvery=$Every",
    (Format-Switch 'ScctShotDir' $shotDir),
    (Format-Switch 'ScctCams' $Cams),
    (Format-Switch 'ExecCmds' $Exec),
    (Format-Switch 'abslog' $log),
    $Extra
) -join ' '

# One ready-made command line, so PowerShell's own quoting of arguments cannot move the quotes.
$run = Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $arguments `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $project "Saved\Logs\run-$Map$Tag.stdout.txt") `
    -RedirectStandardError (Join-Path $project "Saved\Logs\run-$Map$Tag.stderr.txt")
"exit code $($run.ExitCode)"

Select-String -Path $log -Pattern 'LogOpenSCCT' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -First 40
