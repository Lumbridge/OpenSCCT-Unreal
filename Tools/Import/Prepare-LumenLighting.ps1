param(
    [Parameter(Mandatory)] [ValidatePattern('^[A-Za-z0-9_-]+$')] [string] $Map,
    [Parameter(Mandatory)] [string] $Content,
    [string] $Engine = 'F:\UE_5.8',
    [string] $Project = (Resolve-Path "$PSScriptRoot/../..").Path
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../EditorScript.ps1"
$asset = Get-Item -LiteralPath "$Project/Content/Imported/Maps/$Map/$Map.umap"
$backup = Join-Path $Project ("Saved/LumenLighting/" + $Map + "/" + [DateTime]::Now.ToString('yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Copy-Item -LiteralPath $asset.FullName -Destination $backup
$script = "$(Fwd "$PSScriptRoot/scct_lumen_lighting.py") $(Fwd $Content) $Map"
if (!(Invoke-ScctEditorScript -Project $Project -Engine $Engine -Name "prepare-lumen-$Map" -Script $script)) {
    $previous = Join-Path $backup "$Map.umap"
    if (!(Test-Path -LiteralPath $asset.FullName) -or ((Get-FileHash -LiteralPath $asset.FullName).Hash -ne (Get-FileHash -LiteralPath $previous).Hash)) {
        Copy-Item -LiteralPath $previous -Destination $asset.FullName -Force
    }
    throw "Could not prepare $Map for Lumen. The previous level is in $backup."
}
