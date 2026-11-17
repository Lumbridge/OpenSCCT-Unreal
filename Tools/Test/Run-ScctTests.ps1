<#
.SYNOPSIS
Runs the client's named tests as a suite, one game process per test, and answers with an exit code and a JUnit report.

.DESCRIPTION
The port of the Godot client's tools/ci/godot-smoke.ps1. Each test runs in its own process, off-screen by default:

    UnrealEditor-Cmd.exe <project> -game -RenderOffscreen -unattended -ScctTest=<name> -ScctTestTimeout=<s> ...

or the packaged game's exe with the same switches (-Packaged). How each test is run (map, URL options, extra
switches, timeout, tags) comes from Tools/Test/tests.psd1. A run's log is Saved/Logs/test-<name>-<n>.log, n being
the repeat it belongs to.

A test passes when all of these hold:
  - the log has 'SCCT_TEST <name> OK' (Source/OpenSCCT/Test/ScctTestRunnerSubsystem.h writes it)
  - the process ended with exit code 0, by itself, before the time limit
  - the engine went all the way through its shutdown ('LogExit: Exiting.' in the log)
  - no new crash report folder appeared under Saved/Crashes, and the log has no 'Fatal error', no
    'Unhandled Exception' and no ensure
An expected failure (-ExpectedFailures, default selftest-fail) must instead write 'SCCT_TEST <name> FAIL' and end
with exit code 1, still with no crash and no ensure; one that passes counts as a failure, since it means a failure
would no longer be reported.

With -Repeat N every test runs N times and the summary marks a test with mixed verdicts as FLAKY. The script's exit
code is 1 when any run failed (a flaky test's failing run included), else 0.

.PARAMETER Tests
Test names (comma separated or an array). Default: boot, testbox, play-exit.

.PARAMETER All
Every test the game registers (-ScctTestList), except -Exclude, the expected failures (they run anyway, as such) and
the tests tagged helper.

.PARAMETER List
Writes the registered tests with their settings from tests.psd1 and stops.

.PARAMETER ExpectedFailures
Tests that must fail with exit code 1. Pass @() for none.

.PARAMETER Windowed
Runs with a real window instead of off-screen (still -unattended, never takes the mouse); windowed-only tests run too.

.PARAMETER Content
The converted OpenSCCT content folder (-ScctContent). Tests tagged needs-content are skipped without it.

.PARAMETER Repeat
Runs every test this many times.

.PARAMETER JUnit
Where the JUnit XML report goes (default Saved/Test/junit.xml).

.PARAMETER Packaged
The packaged game's exe (OpenSCCTUnreal.exe, or its -Cmd variant): runs the same suite on it instead of the editor
build. Crash reports are looked for next to it as well as in the project.

.PARAMETER Build
Builds the bridge and the editor target first; each build is a step in the report and a failed build stops the run.

.EXAMPLE
Tools\Test\Run-ScctTests.ps1 -Tests selftest-pass,boot,play-exit -Content C:\path\to\.content -JUnit Saved\Test\junit.xml

.EXAMPLE
Tools\Test\Run-ScctTests.ps1 -All -Repeat 3 -Content C:\path\to\.content

.EXAMPLE
Tools\Test\Run-ScctTests.ps1 -Packaged D:\Builds\Windows\OpenSCCTUnreal.exe -Tests boot,play-exit -Content C:\path\to\.content
#>
[CmdletBinding()]
param(
    [Alias('Test')]
    [string[]] $Tests = @('boot', 'testbox', 'play-exit'),
    [switch] $All,
    [string[]] $Exclude = @(),
    [switch] $List,
    [string[]] $ExpectedFailures = @('selftest-fail'),
    [switch] $Windowed,
    [string] $Content = $env:SVM_CONTENT,
    [ValidateRange(1, 100)]
    [int] $Repeat = 1,
    [string] $JUnit = '',
    [string] $Packaged = '',
    [switch] $Build,
    # More switches for every run, as written.
    [string] $Extra = '',
    [string] $Engine = 'F:\UE_5.8'
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$uproject = Join-Path $project 'OpenSCCTUnreal.uproject'
$projectName = [System.IO.Path]::GetFileNameWithoutExtension($uproject)
$editorCmd = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDir = Join-Path $project 'Saved\Logs'
if (-not $JUnit) { $JUnit = Join-Path $project 'Saved\Test\junit.xml' }
$JUnit = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($JUnit)) { $JUnit } else { Join-Path $project $JUnit }))
New-Item -ItemType Directory -Force $logDir | Out-Null

$table = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'tests.psd1')
$suite = if ($Packaged) { 'packaged' } else { 'editor' }
$results = New-Object System.Collections.Generic.List[object]
$ExpectedFailures = @($ExpectedFailures | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

# The game: the editor in game mode on this project, or the packaged exe.
if ($Packaged) {
    $gameExe = [System.IO.Path]::GetFullPath($Packaged)
    if (-not (Test-Path -LiteralPath $gameExe)) { throw "No packaged game at $gameExe." }
    $gamePrefix = @()
} else {
    $gameExe = $editorCmd
    $gamePrefix = @("`"$uproject`"", '-game')
}

# Where a crash leaves its report folder: the project's Saved, and for a packaged game its own Saved (next to the
# exe, or in the user's local app data for an installed build).
$crashDirs = @(Join-Path $project 'Saved\Crashes')
if ($Packaged) {
    $dir = Split-Path -Parent $gameExe
    for ($i = 0; $i -lt 5 -and $dir; $i++) {
        $crashDirs += (Join-Path $dir "$projectName\Saved\Crashes")
        $crashDirs += (Join-Path $dir 'Saved\Crashes')
        $dir = Split-Path -Parent $dir
    }
    if ($env:LOCALAPPDATA) { $crashDirs += (Join-Path $env:LOCALAPPDATA "$projectName\Saved\Crashes") }
}
$crashDirs = @($crashDirs | Select-Object -Unique)

function Format-Switch([string] $Key, [string] $Value) {
    # Unreal reads -Key="a value" whole; a quote inside the value would end it early.
    if ($Value.Contains('"')) { throw "The value of -$Key must not contain a double quote: $Value" }
    "-$Key=`"$Value`""
}

function Get-Setting([string] $Name, [string] $Key, $Default) {
    if ($table.ContainsKey($Name) -and $table[$Name].ContainsKey($Key)) { return $table[$Name][$Key] }
    return $Default
}

function Get-CrashFolders {
    $found = @()
    foreach ($dir in $crashDirs) {
        if (Test-Path -LiteralPath $dir) { $found += @(Get-ChildItem -LiteralPath $dir -Directory | ForEach-Object { $_.FullName }) }
    }
    return $found
}

function Add-Result {
    param([string] $Name, [int] $Attempt, [string] $Verdict, [double] $Seconds, [string] $Message, [string[]] $Output, [string] $Log, [bool] $Expected = $false)
    $results.Add([pscustomobject]@{
        Suite = $suite; Name = $Name; Attempt = $Attempt; Verdict = $Verdict; Seconds = $Seconds
        Message = $Message; Output = $Output; Log = $Log; ExpectedFailure = $Expected
    })
    $colour = switch ($Verdict) { 'pass' { 'Green' } 'skip' { 'DarkYellow' } default { 'Red' } }
    $label = if ($Repeat -gt 1) { "$Name#$Attempt" } else { $Name }
    Write-Host ("[scct] {0,-4} {1} ({2:N1} s){3}" -f $Verdict.ToUpper(), $label, $Seconds, $(if ($Message) { ": $Message" } else { '' })) -ForegroundColor $colour
}

# Starts the game with Arguments, waits up to Timeout seconds (then ends the whole process tree) and returns the
# exit code, whether it was killed, and the time it took.
function Invoke-Game {
    param([string[]] $Arguments, [string] $OutName, [int] $Timeout)
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $gameExe -ArgumentList (($gamePrefix + $Arguments) -join ' ') -NoNewWindow -PassThru `
        -RedirectStandardOutput (Join-Path $logDir "$OutName.stdout.txt") -RedirectStandardError (Join-Path $logDir "$OutName.stderr.txt")
    # Reading the handle now keeps the exit code readable after the process has gone.
    $null = $process.Handle
    $killed = -not $process.WaitForExit($Timeout * 1000)
    if ($killed) { & taskkill.exe /T /F /PID $process.Id 2>&1 | Out-Null }
    $process.WaitForExit()
    $watch.Stop()
    [pscustomobject]@{ ExitCode = if ($killed) { -1 } else { $process.ExitCode }; Killed = $killed; Seconds = $watch.Elapsed.TotalSeconds }
}

function Get-CommonArguments {
    $arguments = @('-unattended', '-nosplash', '-nosound', '-ResX=1280', '-ResY=720', '-ForceRes')
    $arguments += $(if ($Windowed) { '-windowed' } else { '-RenderOffscreen' })
    if ($Content) { $arguments += (Format-Switch 'ScctContent' ($Content.Replace('\', '/'))) }
    if ($Extra) { $arguments += $Extra }
    return $arguments
}

# The tests the game registers, from its -ScctTestList line.
function Get-RegisteredTests {
    $log = (Join-Path $logDir 'test-list.log').Replace('\', '/')
    Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
    $run = Invoke-Game -Arguments (@('-ScctTestList', (Format-Switch 'abslog' $log)) + (Get-CommonArguments)) -OutName 'test-list' -Timeout 300
    $line = if (Test-Path -LiteralPath $log) { Select-String -LiteralPath $log -Pattern 'SCCT_TEST list: (.*)$' | Select-Object -Last 1 } else { $null }
    if ($null -eq $line) { throw "The game wrote no test list (exit code $($run.ExitCode), log $log)." }
    return @($line.Matches[0].Groups[1].Value -split ',\s*' | Where-Object { $_ })
}

# Why a test cannot run in this setup (its tags), or empty when it can.
function Get-SkipReason([string] $Name) {
    $tags = @(Get-Setting $Name 'Tags' @())
    if ('needs-content' -in $tags -and -not $Content) { return 'needs the content folder (-Content or SVM_CONTENT)' }
    if ('windowed-only' -in $tags -and -not $Windowed) { return 'runs only with -Windowed' }
    if (-not $Packaged) {
        # A packaged game carries its levels cooked; only the editor build can be checked for them here.
        $fixture = Get-Setting $Name 'Fixture' ''
        if ('fixture' -in $tags -and $fixture -and -not (Test-Path -LiteralPath (Join-Path $project $fixture))) {
            return "needs the fixture level $fixture (Tools/Test makes it)"
        }
        $map = Get-Setting $Name 'Map' ''
        if ('needs-maps' -in $tags -and $map -and -not (Test-Path -LiteralPath (Join-Path $project "Content\Imported\Maps\$map\$map.umap"))) {
            return "needs the imported map $map (Tools/Import)"
        }
    }
    return ''
}

function Invoke-Test([string] $Name, [int] $Attempt, [bool] $ExpectFailure) {
    $log = (Join-Path $logDir "test-$Name-$Attempt.log").Replace('\', '/')
    $skip = Get-SkipReason $Name
    if ($skip) {
        Add-Result -Name $Name -Attempt $Attempt -Verdict 'skip' -Seconds 0 -Message $skip -Output @() -Log '' -Expected $ExpectFailure
        return
    }

    $timeout = [double](Get-Setting $Name 'Timeout' 120)
    $arguments = @("-ScctTest=$Name", "-ScctTestTimeout=$timeout", (Format-Switch 'abslog' $log)) + (Get-CommonArguments)
    $map = Get-Setting $Name 'Map' ''
    if ($map) { $arguments += (Format-Switch 'ScctTestMap' $map) }
    $options = Get-Setting $Name 'Options' ''
    if ($options) { $arguments += (Format-Switch 'ScctTestOptions' $options) }
    $more = Get-Setting $Name 'Extra' ''
    if ($more) { $arguments += $more }

    Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
    $crashesBefore = @(Get-CrashFolders)
    # The editor needs time to start and to shut down on top of the test's own limit.
    $run = Invoke-Game -Arguments $arguments -OutName "test-$Name-$Attempt" -Timeout ([int][math]::Ceiling($timeout) + 120)
    $output = if (Test-Path -LiteralPath $log) { @(Get-Content -LiteralPath $log) } else { @() }
    $newCrashes = @(Get-CrashFolders | Where-Object { $_ -notin $crashesBefore })

    $verdictLine = @($output | Where-Object { $_ -match "SCCT_TEST $([regex]::Escape($Name)) (OK|FAIL)\b" } | Select-Object -Last 1)
    $verdict = if ($verdictLine.Count -gt 0) { ($verdictLine[0] -replace '^.*?(SCCT_TEST )', '$1') } else { '' }
    $problems = @()
    if ($run.Killed) { $problems += "killed after $([int][math]::Ceiling($timeout) + 120) s" }
    if ($newCrashes.Count -gt 0) { $problems += "crash report $(($newCrashes | ForEach-Object { Split-Path -Leaf $_ }) -join ', ')" }
    foreach ($pattern in @('Fatal error', 'Unhandled Exception', 'Ensure condition failed', '=== Handled ensure', 'Assertion failed')) {
        $hit = @($output | Where-Object { $_.Contains($pattern) } | Select-Object -First 1)
        if ($hit.Count -gt 0) { $problems += "log: $($hit[0].Trim())" }
    }
    if (-not $run.Killed) {
        if ($ExpectFailure) {
            if ($verdict -like "SCCT_TEST $Name OK*") { $problems += "expected to fail but passed ($verdict)" }
            elseif ($run.ExitCode -ne 1) { $problems += "exit code $($run.ExitCode), expected 1. $verdict" }
            elseif (-not $verdict) { $problems += "exit code 1 but no 'SCCT_TEST $Name FAIL' line" }
        } else {
            if ($run.ExitCode -ne 0) { $problems += "exit code $($run.ExitCode), expected 0. $verdict" }
            elseif ($verdict -notlike "SCCT_TEST $Name OK*") { $problems += "exit code 0 but no 'SCCT_TEST $Name OK' line. $verdict" }
            elseif (-not ($output | Where-Object { $_.Contains('LogExit: Exiting.') })) { $problems += 'the engine did not finish its shutdown (no LogExit: Exiting.)' }
        }
    }

    $passed = $problems.Count -eq 0
    $message = if ($passed) { "exit $($run.ExitCode): $verdict" } else { $problems -join '; ' }
    Add-Result -Name $Name -Attempt $Attempt -Verdict $(if ($passed) { 'pass' } else { 'fail' }) -Seconds $run.Seconds -Message $message `
        -Output $output -Log $log -Expected $ExpectFailure
}

function Invoke-Step([string] $Name, [scriptblock] $Body) {
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $output = @(& $Body)
        $results.Add([pscustomobject]@{ Suite = 'setup'; Name = $Name; Attempt = 1; Verdict = 'pass'; Seconds = $watch.Elapsed.TotalSeconds
            Message = ''; Output = $output; Log = ''; ExpectedFailure = $false })
        Write-Host ("[scct] PASS setup/{0} ({1:N1} s)" -f $Name, $watch.Elapsed.TotalSeconds) -ForegroundColor Green
        return $true
    } catch {
        $results.Add([pscustomobject]@{ Suite = 'setup'; Name = $Name; Attempt = 1; Verdict = 'fail'; Seconds = $watch.Elapsed.TotalSeconds
            Message = "$_"; Output = @("$_"); Log = ''; ExpectedFailure = $false })
        Write-Host ("[scct] FAIL setup/{0}: {1}" -f $Name, $_) -ForegroundColor Red
        return $false
    }
}

function Write-JUnit([string] $Path) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $invariant = [System.Globalization.CultureInfo]::InvariantCulture
    # Control characters other than tab and line breaks are not allowed in XML 1.0.
    $clean = { param([string] $Text) [regex]::Replace($Text, '[\x00-\x08\x0B\x0C\x0E-\x1F\uFFFE\uFFFF]', '') }
    $count = { param($Items, [string] $Verdict) @($Items | Where-Object { $_.Verdict -eq $Verdict }).Count }
    $writer = [System.Xml.XmlWriter]::Create($Path, $settings)
    try {
        $writer.WriteStartDocument()
        $writer.WriteStartElement('testsuites')
        $writer.WriteAttributeString('name', 'scct-tests')
        $writer.WriteAttributeString('tests', [string]$results.Count)
        $writer.WriteAttributeString('failures', [string](& $count $results 'fail'))
        $writer.WriteAttributeString('skipped', [string](& $count $results 'skip'))
        $writer.WriteAttributeString('time', ([double](($results | Measure-Object -Property Seconds -Sum).Sum)).ToString('0.000', $invariant))
        foreach ($group in ($results | Group-Object -Property Suite)) {
            $cases = @($group.Group)
            $writer.WriteStartElement('testsuite')
            $writer.WriteAttributeString('name', "scct.$($group.Name)")
            $writer.WriteAttributeString('tests', [string]$cases.Count)
            $writer.WriteAttributeString('failures', [string](& $count $cases 'fail'))
            $writer.WriteAttributeString('errors', '0')
            $writer.WriteAttributeString('skipped', [string](& $count $cases 'skip'))
            $writer.WriteAttributeString('time', ([double](($cases | Measure-Object -Property Seconds -Sum).Sum)).ToString('0.000', $invariant))
            $writer.WriteAttributeString('timestamp', (Get-Date).ToString('s', $invariant))
            foreach ($case in $cases) {
                $writer.WriteStartElement('testcase')
                $writer.WriteAttributeString('classname', "scct.$($case.Suite)")
                $name = if ($Repeat -gt 1 -and $case.Suite -ne 'setup') { "$($case.Name)#$($case.Attempt)" } else { $case.Name }
                if ($case.ExpectedFailure) { $name += ' (expected failure)' }
                $writer.WriteAttributeString('name', $name)
                $writer.WriteAttributeString('time', ([double]$case.Seconds).ToString('0.000', $invariant))
                if ($case.Verdict -eq 'fail') {
                    $writer.WriteStartElement('failure')
                    $writer.WriteAttributeString('message', (& $clean $case.Message))
                    $writer.WriteAttributeString('type', 'failure')
                    $tail = @($case.Output | Select-Object -Last 60) -join "`n"
                    $writer.WriteString((& $clean "$($case.Message)`n`nlog: $($case.Log)`n--- last lines ---`n$tail"))
                    $writer.WriteEndElement()
                } elseif ($case.Verdict -eq 'skip') {
                    $writer.WriteStartElement('skipped')
                    $writer.WriteAttributeString('message', (& $clean $case.Message))
                    $writer.WriteEndElement()
                }
                $lines = @($case.Output | Where-Object { $_ -match 'SCCT_|LogScctTest|Session \d+ ->' })
                if ($case.Verdict -eq 'pass' -and $case.Message) { $lines = @($case.Message) + $lines }
                if ($lines.Count -gt 0) { $writer.WriteElementString('system-out', (& $clean ($lines -join "`n"))) }
                $writer.WriteEndElement()
            }
            $writer.WriteEndElement()
        }
        $writer.WriteEndElement()
        $writer.WriteEndDocument()
    } finally {
        $writer.Dispose()
    }
}

# One row per test: how often it passed, and whether that is stable, failing or flaky.
function Write-Summary {
    $rows = $results | Where-Object { $_.Suite -ne 'setup' } | Group-Object -Property Name | ForEach-Object {
        $runs = @($_.Group)
        $pass = @($runs | Where-Object { $_.Verdict -eq 'pass' }).Count
        $fail = @($runs | Where-Object { $_.Verdict -eq 'fail' }).Count
        $skip = @($runs | Where-Object { $_.Verdict -eq 'skip' }).Count
        $kind = if ($skip -eq $runs.Count) { 'skipped' } elseif ($fail -eq 0) { 'stable' } elseif ($pass -eq 0) { 'failing' } else { 'FLAKY' }
        [pscustomobject]@{
            Test = $_.Name
            Expect = if ($runs[0].ExpectedFailure) { 'FAIL' } else { 'OK' }
            Runs = $runs.Count; Pass = $pass; Fail = $fail; Skip = $skip
            Kind = $kind
            'Avg s' = [math]::Round((($runs | Measure-Object -Property Seconds -Average).Average), 1)
            Last = $runs[-1].Message
        }
    }
    $order = @{ 'FLAKY' = 0; 'failing' = 1; 'stable' = 2; 'skipped' = 3 }
    Write-Host ''
    Write-Host ($rows | Sort-Object -Property @{ Expression = { $order[$_.Kind] } }, Test | Format-Table -AutoSize -Wrap | Out-String).TrimEnd()
}

if ($List) {
    $names = Get-RegisteredTests
    $names | ForEach-Object {
        [pscustomobject]@{
            Test = $_
            Timeout = Get-Setting $_ 'Timeout' 120
            Map = Get-Setting $_ 'Map' ''
            Options = Get-Setting $_ 'Options' ''
            Tags = (@(Get-Setting $_ 'Tags' @()) + $(if ($_ -in $ExpectedFailures) { @('expected-failure') } else { @() })) -join ', '
        }
    } | Format-Table -AutoSize | Out-String | Write-Host
    exit 0
}

$ok = $true
if ($Build) {
    $ok = Invoke-Step 'bridge' {
        $out = & dotnet build -c Release (Join-Path $project 'Bridge\OpenScct.UnrealBridge') -v q -nologo 2>&1
        if ($LASTEXITCODE -ne 0) { throw "dotnet build failed: $(($out | Select-Object -Last 10) -join ' | ')" }
        $out
    }
    if ($ok) {
        $ok = Invoke-Step 'editor' {
            $out = & (Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat') OpenSCCTEditor Win64 Development "-project=$uproject" -waitmutex 2>&1
            if ($LASTEXITCODE -ne 0) { throw "Unreal build failed: $(($out | Where-Object { $_ -match 'error' } | Select-Object -First 10) -join ' | ')" }
            $out
        }
    }
}

if ($ok) {
    $names = @($Tests | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($All) {
        $names = @(Get-RegisteredTests | Where-Object {
            $_ -notin $Exclude -and $_ -notin $ExpectedFailures -and 'helper' -notin @(Get-Setting $_ 'Tags' @())
        })
    }
    $names = @($names | Where-Object { $_ -notin $ExpectedFailures })
    Write-Host ("[scct] {0} suite: {1}; expected failures: {2}; {3} time(s); {4}" -f $suite, ($names -join ', '),
        $(if ($ExpectedFailures.Count) { $ExpectedFailures -join ', ' } else { 'none' }), $Repeat, $(if ($Windowed) { 'windowed' } else { 'off-screen' }))
    for ($attempt = 1; $attempt -le $Repeat; $attempt++) {
        foreach ($name in $names) { Invoke-Test -Name $name -Attempt $attempt -ExpectFailure $false }
        foreach ($name in $ExpectedFailures) { Invoke-Test -Name $name -Attempt $attempt -ExpectFailure $true }
    }
}

Write-JUnit -Path $JUnit
Write-Summary
$failed = @($results | Where-Object { $_.Verdict -eq 'fail' })
$skipped = @($results | Where-Object { $_.Verdict -eq 'skip' })
Write-Host ("[scct] {0} of {1} run(s) passed, {2} failed, {3} skipped; report: {4}" -f
    @($results | Where-Object { $_.Verdict -eq 'pass' }).Count, $results.Count, $failed.Count, $skipped.Count, $JUnit)
if ($failed.Count -gt 0) { exit 1 }
exit 0
