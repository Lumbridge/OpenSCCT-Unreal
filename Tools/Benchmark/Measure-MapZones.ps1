param(
    [string[]]$Maps = @('MounE', 'AquaE'),
    [switch]$Diagnose,
    [string]$Engine = 'F:/UE_5.8',
    [string]$Content = $env:SVM_CONTENT,
    [int]$Width = 2560,
    [int]$Height = 1440
)
$ErrorActionPreference = 'Stop'
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT to read map lighting zones.' }
$Content = (Resolve-Path -LiteralPath $Content).Path
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$run = Join-Path $root ('Saved/ZoneSurveys/' + (Get-Date -Format 'yyyyMMdd-HHmmssfff'))
foreach ($map in $Maps) {
    if ($map -notmatch '^[a-zA-Z0-9_-]+$' -or !(Test-Path "$root/Content/Imported/Maps/$map/$map.umap")) {
        throw "Missing imported map $map"
    }
    $lighting = Join-Path $Content "svmap/$map/render/lighting.json"
    if (-not (Test-Path -LiteralPath $lighting -PathType Leaf)) { throw "No converted map lighting at $lighting." }
    $dir = Join-Path $run $map
    New-Item $dir -ItemType Directory -Force | Out-Null
    $settings = @{ map=$map; output=$dir; content=$Content; diagnose=$Diagnose.IsPresent; viewport=@($Width, $Height) }
    $previousSettings = $env:SCCT_SURVEY_JSON
    $env:SCCT_SURVEY_JSON = $settings | ConvertTo-Json -Compress
    $level = "/Game/Imported/Maps/$map/${map}?ScctMap=${map}?Mode=Story?Side=merc?Bots=2?Practice=1?PlayLevel=1"
    $arguments = @(
        ('"' + "$root/OpenSCCTUnreal.uproject" + '"'), $level,
        '-game','-d3d12','-windowed','-ForceRes','-nosplash','-Multiprocess',
        "-ResX=$Width","-ResY=$Height",'-ScctNoAccountSync','-ScctNoTokenStore','-ScctNoPresenceLease',
        ('-ScctContent="' + $Content + '"'),('-UserDir="' + "$dir/profile" + '"'),
        ('-abslog="' + "$dir/game.log" + '"'),('-ExecCmds="py ' + ($PSScriptRoot -replace '\\','/') + '/survey_zones.py"')
    )
    Write-Host "Profiling $map into $dir"
    $process = Start-Process "$Engine/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru
    try {
        if (!$process.WaitForExit(600000)) { $process.Kill(); throw "Survey timed out for $map" }
        $last = if ($Diagnose) { 'repeat' } else { 'baseline' }
        if ($process.ExitCode -or !(Test-Path "$dir/$last.json") -or !(Test-Path "$dir/profile/Saved/Profiling/CSV/$last.csv")) {
            throw "Incomplete $map survey. See $dir/game.log"
        }
        $captures = if ($Diagnose) { 'baseline','taa','conventional_lights','buffered_queries','half_resolution','repeat' } else { 'baseline' }
        & python "$PSScriptRoot/summarize_zone_survey.py" $dir @captures
        if ($LASTEXITCODE) { throw "Could not read the $map timing data" }
    } finally { $env:SCCT_SURVEY_JSON = $previousSettings }
}
