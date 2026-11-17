<#
.SYNOPSIS
Downloads the CC0 assets of Tools\Import\pbr_sets.json, imports them and rebuilds the master materials
(headless: no editor window). Needed once per checkout only if Content\OpenSCCT is missing them, and again after a
change to pbr_sets.json or scct_materials.py. Re-import the maps afterwards (Import-Map.ps1).

.EXAMPLE
Tools\Import\Import-Assets.ps1                 # everything
Tools\Import\Import-Assets.ps1 -MaterialsOnly  # only rebuild M_ScctSurface, M_ScctGlass, M_ScctWater, M_ScctSky
#>
param(
    [switch] $MaterialsOnly,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
$packed = Join-Path $project 'Saved\ScctImport\ThirdParty'

if (-not $MaterialsOnly) {
    # -I: the downloads are data; nothing beside them may be picked up as Python code.
    python -I -B "$PSScriptRoot\fetch_assets.py" (Join-Path $project 'Saved\Downloads') $packed
    if ($LASTEXITCODE -ne 0) { throw 'Fetching the assets failed.' }
}

# Unreal's command line treats backslashes in -script as escapes.
function Fwd([string] $path) { $path -replace '\\', '/' }
$log = Fwd (Join-Path $project 'Saved\Logs\import-assets.log')
$script = "$(Fwd "$PSScriptRoot\import_assets.py") $(Fwd $packed)"
if ($MaterialsOnly) { $script += ' materials' }
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Fwd "$project\OpenSCCTUnreal.uproject") -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi "-abslog=$log" *> $null
$code = $LASTEXITCODE

Select-String -Path $log -CaseSensitive -Pattern 'SCCT-IMPORT|Traceback|LogPython: Error' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -Unique
"import exit code $code (log: $log)"
