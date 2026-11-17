<#
.SYNOPSIS
Imports the hub's map into the Unreal project as the level /Game/Imported/Maps/Hub/Hub (headless).

.DESCRIPTION
The hub is an OpenSCCT addition: its map is not converted from the original game but kept in the OpenSCCT checkout
as fixtures/hub, in the same SVMap layout as a converted map. Import-Map.ps1 reads a map from <content>\svmap\<Map>
and the shared packs from <content>\shared, so this script lays out such a folder under Saved\ScctImport\HubContent
from two junctions (svmap\Hub to the checkout's fixtures\hub, shared to the converted content's shared) and imports
from there. The bridge finds the hub map in the same folder when a hub is joined (HubBridge's map roots).

.EXAMPLE
Tools\Import\Import-HubMap.ps1 -Content C:\path\to\.content
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    # The OpenSCCT checkout the bridge is built against (its fixtures\hub is the hub map).
    [string] $OpenScct = "$PSScriptRoot\..\..\..\OSC-unreal-bridge",
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Content) { throw 'Pass -Content <converted content folder> or set SVM_CONTENT.' }
$hub = Join-Path $OpenScct 'fixtures\hub'
if (-not (Test-Path (Join-Path $hub 'manifest.json'))) { throw "No hub map at $hub (pass -OpenScct <OpenSCCT checkout>)." }
$shared = Join-Path $Content 'shared'
if (-not (Test-Path $shared)) { throw "No shared packs at $shared." }

# Junctions, not copies: the hub map follows the checkout, and nothing is written into either source.
$root = Join-Path $project 'Saved\ScctImport\HubContent'
New-Item -ItemType Directory -Force (Join-Path $root 'svmap') | Out-Null
function Set-Junction([string] $Path, [string] $Target) {
    $existing = Get-Item $Path -ErrorAction SilentlyContinue
    if ($existing) {
        if ($existing.LinkType -eq 'Junction' -and (Resolve-Path $existing.Target).Path -eq (Resolve-Path $Target).Path) { return }
        if ($existing.LinkType -ne 'Junction') { throw "$Path exists and is not a junction; remove it first." }
        # rmdir takes away the link only, never what it points to.
        cmd /c rmdir "$Path" | Out-Null
    }
    New-Item -ItemType Junction -Path $Path -Target (Resolve-Path $Target).Path | Out-Null
}
Set-Junction (Join-Path $root 'svmap\Hub') $hub
Set-Junction (Join-Path $root 'shared') $shared
# The pads' matches and places (hub.json, areas.json): a hub hosted from the game finds them beside the hub map,
# else it falls back to built-in pads that are not where the hub map draws them.
$hubmap = Join-Path $OpenScct 'fixtures\hubmap'
if (Test-Path (Join-Path $hubmap 'hub.json')) { Set-Junction (Join-Path $root 'svmap\hubmap') $hubmap }
Write-Host "Hub content laid out at $root"

& "$PSScriptRoot\Import-Map.ps1" -Map Hub -Content $root -Engine $Engine
exit $LASTEXITCODE
