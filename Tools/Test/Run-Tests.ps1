<#
.SYNOPSIS
Runs named client tests off-screen (no window, never takes focus) and ends with their verdict as the exit code.

.DESCRIPTION
Starts the game with -ScctTest=<names> (Source/OpenSCCT/Test/ScctTestRunnerSubsystem.h), writes its log to
Saved\Logs\test-<Tag>.log and prints the SCCT_TEST lines. Exit code: 0 every test passed, 1 one failed, 2 a name is
unknown (the known names are printed), anything else: the game did not finish (a crash). The tests open the levels
they need; testbox and boot use the TestBox fixture (Tools\Test\Make-TestBox.ps1 makes its level).
This is one process for a quick look; Tools\Test\Run-ScctTests.ps1 is the suite driver (one process per test, crash
and ensure checks, expected failures, repeats, JUnit report, packaged builds).

.EXAMPLE
Tools\Test\Run-Tests.ps1 -Tests selftest-pass

.EXAMPLE
Tools\Test\Run-Tests.ps1 -Tests boot,testbox -Content C:\path\to\.content

.EXAMPLE
Tools\Test\Run-Tests.ps1 -List
#>
param(
    # Test names, run in this order.
    [string[]] $Tests = @(),
    [string] $Content = $env:SVM_CONTENT,
    # Seconds each test may run.
    [double] $Timeout = 120,
    # Writes the known names instead of running anything.
    [switch] $List,
    # Added to the log's name (test-<Tag>.log); default: the test names.
    [string] $Tag = '',
    # Anything else for the command line, as written.
    [string] $Extra = '',
    [string] $Engine = 'F:\UE_5.8'
)

$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$names = ($Tests | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) -join ','
if (-not $Tag) { $Tag = if ($List) { 'list' } else { $names.Replace(',', '+') } }
$log = (Join-Path $project "Saved\Logs\test-$Tag.log").Replace('\', '/')
New-Item -ItemType Directory -Force (Join-Path $project 'Saved\Logs') | Out-Null
Remove-Item $log -ErrorAction SilentlyContinue

$arguments = @(
    "`"$project\OpenSCCTUnreal.uproject`"",
    '-game', '-RenderOffscreen', '-ResX=1280', '-ResY=720', '-ForceRes', '-unattended', '-nosplash', '-nosound',
    $(if ($List) { '-ScctTestList' } else { "-ScctTest=$names" }),
    "-ScctTestTimeout=$Timeout",
    "-abslog=`"$log`"",
    $Extra
)
if ($Content) { $arguments += "-ScctContent=`"$($Content.Replace('\', '/'))`"" }

$run = Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList ($arguments -join ' ') `
    -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $project "Saved\Logs\test-$Tag.stdout.txt") `
    -RedirectStandardError (Join-Path $project "Saved\Logs\test-$Tag.stderr.txt")

if (Test-Path $log) {
    Select-String -Path $log -Pattern 'SCCT_TEST ' | ForEach-Object { $_.Line -replace '^.*?(SCCT_TEST )', '$1' }
}
"exit code $($run.ExitCode) (log: $log)"
exit $run.ExitCode
