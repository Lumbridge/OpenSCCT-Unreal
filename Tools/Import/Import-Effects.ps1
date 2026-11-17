<#
.SYNOPSIS
Rebuilds the effect textures, materials and Niagara systems of Content\OpenSCCT\Effects (headless: no editor
window). Needed only after a change to Tools\Import\scct_effects.py, scct_niagara.py or make_effect_textures.py.
Re-import the maps afterwards.

.EXAMPLE
Tools\Import\Import-Effects.ps1
#>
param(
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$textures = Join-Path $project 'Saved\ScctImport\Effects'

python -B "$PSScriptRoot\make_effect_textures.py" $textures
if ($LASTEXITCODE -ne 0) { throw 'Drawing the effect textures failed.' }

# Unreal's command line treats backslashes in -script as escapes.
function Fwd([string] $path) { $path -replace '\\', '/' }
$log = Fwd (Join-Path $project 'Saved\Logs\import-effects.log')
$script = "$(Fwd "$PSScriptRoot\import_effects.py") $(Fwd $textures)"
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Fwd "$project\OpenSCCTUnreal.uproject") -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi "-abslog=$log" *> $null
$code = $LASTEXITCODE

Select-String -Path $log -CaseSensitive -Pattern 'SCCT-IMPORT|Traceback|LogPython: Error' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -Unique
# The headless editor ends with code 1 on engine start-up messages that have nothing to do with this script.
"effects build exit code $code (log: $log)"
