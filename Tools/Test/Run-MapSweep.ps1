<#
.SYNOPSIS
Plays every imported map off-screen in each mode and reports load time, missing assets and broken pixels per map.

.DESCRIPTION
The port of the Godot client's map-shots sweep. For every level under Content\Imported\Maps (or -Maps) and every
mode (-Modes, Story and Deathmatch by default) it starts one game process with the named test map-shots
(Source/OpenSCCT/Test/ScctMapShotsTest.cpp):

    UnrealEditor-Cmd.exe <project> -game -RenderOffscreen -unattended -ScctTest=map-shots -ScctTestMap=<Map> -ScctMapShotsMode=<Mode> ...

and reads its 'SCCT_MAPSHOTS <Map> mode=.. load=.. missing=m/t/mat black=.. magenta=.. views=..' line. A run passes
when the process ends with exit code 0 and the log has 'SCCT_TEST map-shots OK'; a map that does not load, a spy that
never appears, a view over the magenta threshold, a crash or a run past the time limit fails with its reason.

It writes, under -Out (default Saved\Test\map-sweep):
    map-sweep.csv     one row per map and mode
    junit.xml         one test case per map and mode
    shots\            the eight views of each run (four in plain sight, four in night vision)
    sheet-<Map>-<Mode>.png   the eight views of one run on one sheet (Tools\Look\contact_sheet.py, needs Python with Pillow)
    sheet.png         the first view of every run
Logs go to Saved\Logs\mapsweep-<Map>-<Mode>.log. The exit code is 1 when any run failed, else 0.

.EXAMPLE
Tools\Test\Run-MapSweep.ps1 -Content C:\path\to\.content

.EXAMPLE
Tools\Test\Run-MapSweep.ps1 -Maps AquaE,SteeE -Modes Story -Content C:\path\to\.content
#>
[CmdletBinding()]
param(
    # Map names; default: every folder of Content\Imported\Maps that has its level.
    [string[]] $Maps = @(),
    [string[]] $Exclude = @(),
    [string[]] $Modes = @('Story', 'Deathmatch'),
    [string] $Content = $env:SVM_CONTENT,
    # Seconds one run's test may take; the process is killed 120 s after that.
    [int] $Timeout = 300,
    # The share of magenta pixels in one view (percent) above which a run fails.
    [double] $Magenta = 2.0,
    [string] $Out = '',
    [string] $Extra = '',
    [string] $Python = 'python',
    [string] $Engine = 'F:\UE_5.8'
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$uproject = Join-Path $project 'OpenSCCTUnreal.uproject'
$editorCmd = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDir = Join-Path $project 'Saved\Logs'
if (-not $Out) { $Out = Join-Path $project 'Saved\Test\map-sweep' }
$Out = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $project $Out }))
$shotDir = Join-Path $Out 'shots'
New-Item -ItemType Directory -Force $logDir, $Out, $shotDir | Out-Null
if (-not $Content) { throw 'The sweep needs the converted content folder (-Content or SVM_CONTENT).' }
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

$split = { param($Values) @($Values | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
$Maps = & $split $Maps
$Exclude = & $split $Exclude
$Modes = & $split $Modes
if ($Maps.Count -eq 0) {
    $mapRoot = Join-Path $project 'Content\Imported\Maps'
    if (-not (Test-Path -LiteralPath $mapRoot)) { throw "No imported maps under $mapRoot (Tools\Import\Import-Map.ps1 makes them)." }
    $Maps = @(Get-ChildItem -LiteralPath $mapRoot -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "$($_.Name).umap") } |
        ForEach-Object { $_.Name } | Sort-Object)
}
$Maps = @($Maps | Where-Object { $_ -notin $Exclude })
if ($Maps.Count -eq 0) { throw 'No maps to sweep.' }

function Format-Switch([string] $Key, [string] $Value) {
    if ($Value.Contains('"')) { throw "The value of -$Key must not contain a double quote: $Value" }
    "-$Key=`"$Value`""
}

$rows = New-Object System.Collections.Generic.List[object]
Write-Host ("[sweep] {0} map(s) x {1} mode(s): {2}" -f $Maps.Count, $Modes.Count, ($Maps -join ', '))
foreach ($map in $Maps) {
    foreach ($mode in $Modes) {
        $name = "$map-$mode"
        $log = (Join-Path $logDir "mapsweep-$name.log").Replace('\', '/')
        Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
        Get-ChildItem -LiteralPath $shotDir -Filter "$name-*.png" | Remove-Item -Force
        $arguments = @(
            "`"$uproject`"", '-game', '-ScctTest=map-shots', "-ScctTestTimeout=$Timeout", (Format-Switch 'abslog' $log),
            '-unattended', '-nosplash', '-nosound', '-ResX=1280', '-ResY=720', '-ForceRes', '-RenderOffscreen',
            (Format-Switch 'ScctContent' ($Content.Replace('\', '/'))), (Format-Switch 'ScctTestMap' $map),
            (Format-Switch 'ScctMapShotsMode' $mode), (Format-Switch 'ScctMapShotsDir' ($shotDir.Replace('\', '/'))),
            ('-ScctMapShotsMagenta=' + $Magenta.ToString($invariant)), $Extra
        ) -join ' '
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        $process = Start-Process -FilePath $editorCmd -ArgumentList $arguments -NoNewWindow -PassThru `
            -RedirectStandardOutput (Join-Path $logDir "mapsweep-$name.stdout.txt") -RedirectStandardError (Join-Path $logDir "mapsweep-$name.stderr.txt")
        $null = $process.Handle
        $killed = -not $process.WaitForExit(($Timeout + 120) * 1000)
        if ($killed) { & taskkill.exe /T /F /PID $process.Id 2>&1 | Out-Null }
        $process.WaitForExit()
        $watch.Stop()
        $exit = if ($killed) { -1 } else { $process.ExitCode }

        $output = if (Test-Path -LiteralPath $log) { @(Get-Content -LiteralPath $log) } else { @() }
        $row = [ordered]@{
            Map = $map; Mode = $mode; Verdict = 'fail'; Reason = ''; LoadSeconds = ''; MissingMeshes = ''; MissingTextures = ''
            MissingMaterials = ''; BlackPercent = ''; MagentaPercent = ''; Views = ''; MissingExamples = ''; Seconds = [math]::Round($watch.Elapsed.TotalSeconds, 1)
            ExitCode = $exit; Log = $log
        }
        $line = @($output | Where-Object { $_ -match 'SCCT_MAPSHOTS \S+ mode=' } | Select-Object -Last 1)
        if ($line.Count -gt 0 -and $line[0] -match 'load=(-?[\d.]+) missing=(\d+)/(\d+)/(\d+) black=(-?[\d.]+) magenta=(-?[\d.]+) views=(\S+)') {
            $row.LoadSeconds = $Matches[1]; $row.MissingMeshes = $Matches[2]; $row.MissingTextures = $Matches[3]; $row.MissingMaterials = $Matches[4]
            $row.BlackPercent = $Matches[5]; $row.MagentaPercent = $Matches[6]; $row.Views = $Matches[7]
        }
        $examples = @($output | Where-Object { $_ -match 'SCCT_MAPSHOTS_MISSING \S+: (.*)$' } | Select-Object -Last 1)
        if ($examples.Count -gt 0) { $row.MissingExamples = ($examples[0] -replace '^.*SCCT_MAPSHOTS_MISSING \S+: ', '') }

        $verdict = @($output | Where-Object { $_ -match 'SCCT_TEST map-shots (OK|FAIL)\b' } | Select-Object -Last 1)
        $verdictText = if ($verdict.Count -gt 0) { $verdict[0] -replace '^.*?(SCCT_TEST )', '$1' } else { '' }
        $problems = @()
        if ($killed) { $problems += "killed after $($Timeout + 120) s" }
        foreach ($pattern in @('Fatal error', 'Unhandled Exception', 'Assertion failed')) {
            $hit = @($output | Where-Object { $_.Contains($pattern) } | Select-Object -First 1)
            if ($hit.Count -gt 0) { $problems += "log: $($hit[0].Trim())" }
        }
        if (-not $killed) {
            if ($verdictText -like 'SCCT_TEST map-shots FAIL*') { $problems += ($verdictText -replace '^SCCT_TEST map-shots FAIL:?\s*', '') }
            elseif ($exit -ne 0) { $problems += "exit code $exit and no verdict line" }
            elseif ($verdictText -notlike 'SCCT_TEST map-shots OK*') { $problems += "exit code 0 but no 'SCCT_TEST map-shots OK' line" }
        }
        if ($problems.Count -eq 0) { $row.Verdict = 'pass' } else { $row.Reason = $problems -join '; ' }
        $rows.Add([pscustomobject]$row)
        Write-Host ("[sweep] {0,-4} {1} ({2:N0} s) load={3} missing={4}/{5}/{6} black={7} magenta={8}{9}" -f $row.Verdict.ToUpper(), $name, $row.Seconds,
            $row.LoadSeconds, $row.MissingMeshes, $row.MissingTextures, $row.MissingMaterials, $row.BlackPercent, $row.MagentaPercent,
            $(if ($row.Reason) { ": $($row.Reason)" } else { '' })) -ForegroundColor $(if ($row.Verdict -eq 'pass') { 'Green' } else { 'Red' })

        # The run's eight views on one sheet.
        $shots = @(Get-ChildItem -LiteralPath $shotDir -Filter "$name-*.png" | Sort-Object { $_.Name -match '-nv\d' }, Name | ForEach-Object { $_.FullName })
        if ($shots.Count -gt 0) {
            try { & $Python (Join-Path $project 'Tools\Look\contact_sheet.py') (Join-Path $Out "sheet-$name.png") @shots | Out-Null } catch { Write-Host "[sweep] no sheet for ${name}: $_" -ForegroundColor DarkYellow }
        }
    }
}

$csv = Join-Path $Out 'map-sweep.csv'
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding utf8

# JUnit: one case per map and mode.
$junit = Join-Path $Out 'junit.xml'
$settings = New-Object System.Xml.XmlWriterSettings
$settings.Indent = $true
$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
$clean = { param([string] $Text) [regex]::Replace($Text, '[\x00-\x08\x0B\x0C\x0E-\x1F\uFFFE\uFFFF]', '') }
$failed = @($rows | Where-Object { $_.Verdict -ne 'pass' })
$writer = [System.Xml.XmlWriter]::Create($junit, $settings)
try {
    $writer.WriteStartDocument()
    $writer.WriteStartElement('testsuites')
    $writer.WriteStartElement('testsuite')
    $writer.WriteAttributeString('name', 'scct.map-sweep')
    $writer.WriteAttributeString('tests', [string]$rows.Count)
    $writer.WriteAttributeString('failures', [string]$failed.Count)
    $writer.WriteAttributeString('errors', '0')
    $writer.WriteAttributeString('time', ([double](($rows | Measure-Object -Property Seconds -Sum).Sum)).ToString('0.000', $invariant))
    $writer.WriteAttributeString('timestamp', (Get-Date).ToString('s', $invariant))
    foreach ($row in $rows) {
        $writer.WriteStartElement('testcase')
        $writer.WriteAttributeString('classname', "scct.map-sweep.$($row.Mode)")
        $writer.WriteAttributeString('name', $row.Map)
        $writer.WriteAttributeString('time', ([double]$row.Seconds).ToString('0.000', $invariant))
        if ($row.Verdict -ne 'pass') {
            $writer.WriteStartElement('failure')
            $writer.WriteAttributeString('message', (& $clean $row.Reason))
            $writer.WriteAttributeString('type', 'failure')
            $writer.WriteString((& $clean "$($row.Reason)`nlog: $($row.Log)"))
            $writer.WriteEndElement()
        }
        $writer.WriteElementString('system-out', (& $clean ("load={0} missing={1}/{2}/{3} black={4} magenta={5} views={6} {7}" -f $row.LoadSeconds, $row.MissingMeshes,
            $row.MissingTextures, $row.MissingMaterials, $row.BlackPercent, $row.MagentaPercent, $row.Views, $row.MissingExamples)))
        $writer.WriteEndElement()
    }
    $writer.WriteEndElement()
    $writer.WriteEndElement()
    $writer.WriteEndDocument()
} finally {
    $writer.Dispose()
}

# The first view of every run on one sheet.
$first = @($rows | ForEach-Object { Join-Path $shotDir "$($_.Map)-$($_.Mode)-0.png" } | Where-Object { Test-Path -LiteralPath $_ })
$sheet = Join-Path $Out 'sheet.png'
if ($first.Count -gt 0) {
    try { & $Python (Join-Path $project 'Tools\Look\contact_sheet.py') $sheet @first | Out-Null } catch { Write-Host "[sweep] no contact sheet: $_" -ForegroundColor DarkYellow }
}

Write-Host ''
Write-Host ($rows | Select-Object Map, Mode, Verdict, LoadSeconds, MissingMeshes, MissingTextures, MissingMaterials, BlackPercent, MagentaPercent, Reason |
    Format-Table -AutoSize -Wrap | Out-String).TrimEnd()
Write-Host ("[sweep] {0} of {1} run(s) passed, {2} failed; csv: {3}; junit: {4}; sheet: {5}" -f ($rows.Count - $failed.Count), $rows.Count, $failed.Count, $csv, $junit, $sheet)
foreach ($row in $failed) { Write-Host ("[sweep] FAILED {0} {1}: {2}" -f $row.Map, $row.Mode, $row.Reason) -ForegroundColor Red }
if ($failed.Count -gt 0) { exit 1 }
exit 0
