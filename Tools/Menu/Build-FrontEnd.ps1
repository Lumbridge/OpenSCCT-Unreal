<#
.SYNOPSIS
Builds the front end level (/Game/OpenSCCT/Maps/L_FrontEnd) in a headless editor: no window.

.DESCRIPTION
The level is the project's start map. It is small and made entirely by build_front_end.py, so it can be rebuilt
at any time; the menu itself is C++ (Source/OpenSCCT/Menu). Build the C++ first: the level names its game mode.

.EXAMPLE
Tools\Menu\Build-FrontEnd.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

# A level that exists cannot be replaced from inside the editor's Python: it is rebuilt from nothing.
Remove-Item (Join-Path $project 'Content\OpenSCCT\Maps\L_FrontEnd.umap') -ErrorAction SilentlyContinue

# Unreal's command line treats backslashes in -script as escapes.
function Fwd([string] $path) { $path -replace '\\', '/' }
$log = Fwd (Join-Path $project 'Saved\Logs\build-front-end.log')
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Fwd "$project\OpenSCCTUnreal.uproject") -run=pythonscript "-script=$(Fwd "$PSScriptRoot\build_front_end.py")" -unattended -nosplash -nullrhi "-abslog=$log" *> $null
$code = $LASTEXITCODE

Select-String -Path $log -CaseSensitive -Pattern 'SCCT-FRONTEND|Traceback|LogPython: Error' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -Unique
"front end build exit code $code (log: $log)"
