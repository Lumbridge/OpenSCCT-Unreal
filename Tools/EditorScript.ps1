<#
.SYNOPSIS
Shared by the asset scripts (dot-source it): runs one editor Python script headless (no window, -nullrhi) and
reports truthfully whether it finished.

.DESCRIPTION
The commandlet's exit code is not the answer: it is 1 whenever the engine logged an error while starting (the
toolset plugins do in a commandlet), whether or not the script worked. Tools\Import\run_script.py runs the script
and writes "SCCT-SCRIPT OK" or "SCCT-SCRIPT FAILED" as its last line; that line decides. Invoke-ScctEditorScript
prints the script's own log lines and returns $true or $false; Exit-ScctScripts ends the calling script with exit
code 0 only when every step so far returned $true.
#>

# Unreal's command line treats backslashes in -script as escapes.
function Fwd([string] $path) { $path.Replace([char]92, [char]47) }

$script:ScctFailedSteps = @()

function Invoke-ScctEditorScript {
    param(
        [Parameter(Mandatory)] [string] $Project,
        [Parameter(Mandatory)] [string] $Engine,
        # Names the log: Saved\Logs\<Name>.log.
        [Parameter(Mandatory)] [string] $Name,
        # The script's full path and its arguments, as one string.
        [Parameter(Mandatory)] [string] $Script,
        # Which of the script's log lines to show.
        [string] $Show = 'SCCT-IMPORT'
    )

    New-Item -ItemType Directory -Force (Join-Path $Project 'Saved\Logs') | Out-Null
    $log = Fwd (Join-Path $Project "Saved\Logs\$Name.log")
    Remove-Item $log -ErrorAction SilentlyContinue
    $runner = Fwd (Join-Path $PSScriptRoot 'Import\run_script.py')
    & "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Fwd "$Project\OpenSCCTUnreal.uproject") -run=pythonscript "-script=$runner $Script" -unattended -nosplash -nullrhi "-abslog=$log" *> $null
    $code = $LASTEXITCODE

    $lines = @()
    if (Test-Path $log) {
        # The log repeats warnings in its closing summary: each line once, without its time stamp.
        $lines = @(Select-String -Path $log -CaseSensitive -Pattern "$Show|SCCT-SCRIPT" |
            Where-Object { $_.Line -notmatch 'LogInit: Display' } |
            ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' -replace '^LogPython: Warning: ', '' } | Select-Object -Unique)
    }
    $lines | Where-Object { $_ -notmatch '^SCCT-SCRIPT OK' } | Write-Host

    $ok = [bool]($lines | Where-Object { $_ -match '^SCCT-SCRIPT OK' })
    if ($ok) {
        Write-Host "$Name finished (log: $log)"
    }
    else {
        Write-Host "$Name FAILED: the script did not finish (editor exit code $code, log: $log)"
        $script:ScctFailedSteps += $Name
    }
    $ok
}

function Exit-ScctScripts {
    if ($script:ScctFailedSteps.Count -gt 0) {
        Write-Host "FAILED: $($script:ScctFailedSteps -join ', ')"
        exit 1
    }
    exit 0
}
