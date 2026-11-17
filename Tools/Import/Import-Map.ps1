<#
.SYNOPSIS
Imports a converted OpenSCCT map into the Unreal project as a level (headless: no editor window).

.EXAMPLE
Tools\Import\Import-Map.ps1 -Map AquaE -Content C:\path\to\.content
#>
param(
    [Parameter(Mandatory)] [string] $Map,
    [string] $Content = $env:SVM_CONTENT,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
$render = Join-Path $Content "svmap\$Map\render"
if (-not (Test-Path $render)) { throw "No converted map at $render." }

$normals = Join-Path $project "Saved\ScctImport\$Map\normals"
python "$PSScriptRoot\prepare_textures.py" $render $normals
if ($LASTEXITCODE -ne 0) { throw 'Texture preparation failed.' }

# A level that exists cannot be replaced from inside the editor's Python: it is rebuilt from nothing each import.
Remove-Item (Join-Path $project "Content\Imported\Maps\$Map\$Map.umap") -ErrorAction SilentlyContinue

# Unreal's command line treats backslashes in -script as escapes.
function Fwd([string] $path) { $path -replace '\\', '/' }
$log = Fwd (Join-Path $project "Saved\Logs\import-$Map.log")
$script = "$(Fwd "$PSScriptRoot\import_map.py") $(Fwd $Content) $Map $(Fwd $normals)"
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" (Fwd "$project\OpenSCCTUnreal.uproject") -run=pythonscript "-script=$script" -unattended -nosplash -nullrhi "-abslog=$log" *> $null
$code = $LASTEXITCODE

Select-String -Path $log -CaseSensitive -Pattern 'SCCT-IMPORT|Traceback|LogPython: Error' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -Unique
"import exit code $code (log: $log)"
