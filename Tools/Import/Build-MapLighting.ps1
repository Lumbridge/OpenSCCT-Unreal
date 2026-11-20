param(
    [Parameter(Mandatory)] [ValidatePattern('^[A-Za-z0-9_-]+$')] [string] $Map,
    [Parameter(Mandatory)] [string] $Content,
    [ValidateSet('Preview','Medium','High','Production')] [string] $Quality = 'High',
    [string] $Engine = 'F:\UE_5.8'
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot/../..").Path
. "$PSScriptRoot/../EditorScript.ps1"
$folder = Join-Path $project "Content/Imported/Maps/$Map"
$level = Get-Item -LiteralPath "$folder/$Map.umap"
$registry = "$folder/${Map}_BuiltData.uasset"
$backup = Join-Path $project ("Saved/NativeLighting/" + $Map + "/" + [DateTime]::Now.ToString('yyyyMMdd-HHmmssfff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Copy-Item -LiteralPath $level.FullName -Destination $backup
if (Test-Path $registry) { Copy-Item -LiteralPath $registry -Destination $backup }
try {
    $script = "$(Fwd "$PSScriptRoot/prepare_native_lighting.py") $(Fwd $Content) $Map"
    $prepared = Invoke-ScctEditorScript -Project $project -Engine $Engine -Name "prepare-lighting-$Map" -Script $script
    if (!$prepared) { throw "Lightmap preparation failed for $Map" }
    if (Select-String -LiteralPath "$project/Saved/Logs/prepare-lighting-$Map.log" -SimpleMatch -Pattern 'SCCT-IMPORT No permanent fixtures', 'SCCT-IMPORT Lightmass skipped:') { return }
    $start = [DateTime]::UtcNow
    $log = "$backup/build.log"
    $runner = Fwd "$PSScriptRoot/bake_gpu_lighting.py"
    & "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$project/OpenSCCTUnreal.uproject" "/Game/Imported/Maps/$Map/$Map" -EnablePlugins=GPULightmass -d3d12 -RenderOffscreen -unattended -NoSplash -NoP4 "-abslog=$log" "-ExecCmds=py $runner $Map $Quality" *> $null
    if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch "SCCT-LIGHTING GPU finished") -or !(Test-Path $registry) -or (Get-Item $registry).LastWriteTimeUtc -lt $start -or
        (Select-String -LiteralPath $log -Pattern 'Failed building lighting|Lighting build failed|Lighting build was cancelled|Fatal error')) {
        throw "Native lighting did not build. See $log"
    }
    Write-Host "$Map native lighting built at $Quality quality: $registry"
} catch {
    Copy-Item -LiteralPath "$backup/$Map.umap" -Destination $level.FullName -Force
    if (Test-Path -LiteralPath "$backup/${Map}_BuiltData.uasset") { Copy-Item -LiteralPath "$backup/${Map}_BuiltData.uasset" -Destination $registry -Force }
    elseif (Test-Path -LiteralPath $registry) { Remove-Item -LiteralPath $registry }
    throw
}
