<#
.SYNOPSIS
Rebuilds the goggles' post-process materials (night, thermal, motion tracking, EMF) headless: no editor window.

.DESCRIPTION
Runs Tools\Materials\build_vision_materials.py in the editor's Python and writes
Content\OpenSCCT\UI\Vision\M_ScctVision*.uasset. Run it after changing the shader text in that script.

.EXAMPLE
Tools\Materials\Build-VisionMaterials.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path

# Unreal's command line treats backslashes in -script as escapes.
function Fwd([string] $path) { $path -replace '\\', '/' }
New-Item -ItemType Directory -Force (Join-Path $project 'Saved\Logs') | Out-Null
$log = Fwd (Join-Path $project 'Saved\Logs\build-vision-materials.log')
$script = Fwd "$PSScriptRoot\build_vision_materials.py"
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Fwd "$project\OpenSCCTUnreal.uproject") -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi "-abslog=$log" *> $null
$code = $LASTEXITCODE

Select-String -Path $log -CaseSensitive -Pattern 'SCCT-VISION|Traceback|LogPython: Error|Error:.*M_ScctVision' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -Unique
"exit code $code (log: $log)"
