#Requires -Version 7
<#
.SYNOPSIS
Cooks the imported maps into the IoStore containers OpenSCCT-Unreal-Content publishes: one shared pack for the
imported content every map uses and one pack per map.

.DESCRIPTION
Runs headless: UnrealBuildTool builds the editor and the game, then RunUAT BuildCookRun cooks, stages and paks with
the MapPaks custom config (Config/Custom/MapPaks), which chunks the cook and hands chunk assignment to
UScctMapPakAssetManager (Source/OpenSCCT/Cook):

  pakchunk0     the game: the engine and this project's own content (what a release ships)
  pakchunk100   the shared pack: /Game/Imported/Chars, /Game/Imported/Fx, and anything in a map's folder that
                another cooked map also uses
  pakchunk101+  one per map, in the order of -Maps: the map's level and what only it uses

Every package goes into exactly one container. The containers keep the names the cook gives them (pakchunk<N>-Windows):
the engine reads the chunk number from the file name to open the container's shader library when it is mounted.

Writes, under -Out (default Saved/MapPaks):

  <cook id>\shared\imported-shared\pakchunk100-Windows.{pak,utoc,ucas}
  <cook id>\maps\<Code>\pakchunk<N>-Windows.{pak,utoc,ucas}
  <cook id>\cook.json        the cook (id, engine, game version, commit) and every pack: chunk, level, files, sizes
                             and SHA-256 (what OpenSCCT-Unreal-Content's tools/ouc.py packs and indexes)
  <cook id>\chunks\          the cook's list of packages in each container, for checking
  <cook id>\uat.log, build-*.log
  Staged\                    UAT's staging folder (the whole staged game, pakchunk0 included)

The cook reads Content/Imported and writes only under Saved/ (the cook's Zen store, staging and -Out).

A pack only loads in a game built from the same engine and the same project content it was cooked against: the cook
id (<engine>-<ProjectVersion>-<Revision>) names that build.

.PARAMETER Maps
The map codes to cook (folders of Content/Imported/Maps holding <Code>/<Code>.umap). Default: every imported map but
TestRange. A publish cooks every map in one run: what counts as shared depends on which maps were cooked together.

.PARAMETER Revision
The n of the cook id, bumped for every re-cook against the same ProjectVersion. Default 1.

.PARAMETER Configuration
The game build staged with the cook: Development (default) or Shipping. The cooked containers are the same either way.

.PARAMETER Out
The output folder. Default Saved/MapPaks.

.PARAMETER Engine
The Unreal Engine 5.8 root (the folder holding Engine\). Default: UE_ROOT, else F:\UE_5.8.

.PARAMETER SkipBuild
Do not build the editor and the game first (they must already be built).

.EXAMPLE
pwsh Tools/Cook/Cook-MapPaks.ps1 -Maps AquaE
pwsh Tools/Cook/Cook-MapPaks.ps1 -Revision 2
#>
param(
    [string[]]$Maps = @(),
    [int]$Revision = 1,
    [ValidateSet('Development', 'Shipping')]
    [string]$Configuration = 'Development',
    [string]$Out = '',
    [string]$Engine = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'F:\UE_5.8' }),
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$projectFile = Get-ChildItem -LiteralPath $repo -Filter '*.uproject' | Select-Object -First 1
if (-not $projectFile) { throw "Cook-MapPaks: no .uproject in $repo" }
$project = $projectFile.FullName
$projectName = $projectFile.BaseName
$sharedChunk = 100
$firstMapChunk = 101
$sharedId = 'imported-shared'

function Fail([string]$message) { throw "Cook-MapPaks: $message" }
function Invoke-Logged([string]$what, [string]$log, [scriptblock]$command) {
    & $command *> $log
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit code $LASTEXITCODE; see $log)" }
}
function Get-Sha256([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }

# --- What to cook --------------------------------------------------------------------------------------------------
$mapsRoot = Join-Path $repo 'Content/Imported/Maps'
if (-not (Test-Path -LiteralPath $mapsRoot)) { Fail "no imported maps at $mapsRoot (Tools/Import/Import-Map.ps1)" }
$imported = @(Get-ChildItem -LiteralPath $mapsRoot -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "$($_.Name).umap") } |
    ForEach-Object Name | Sort-Object)
if ($Maps.Count -eq 0) { $Maps = @($imported | Where-Object { $_ -ne 'TestRange' }) }
$Maps = @($Maps | ForEach-Object { $_ -split '[,+ ]' } | Where-Object { $_ })
foreach ($code in $Maps) {
    if ($code -notmatch '^[A-Za-z0-9._-]+$') { Fail "'$code' is not a map code" }
    if ($code -notin $imported) { Fail "$code is not imported (no Content/Imported/Maps/$code/$code.umap)" }
}
if (($Maps | Sort-Object -Unique).Count -ne $Maps.Count) { Fail 'a map is listed twice' }
if ($Maps.Count -eq 0) { Fail 'no maps to cook' }

# --- The cook's identity -------------------------------------------------------------------------------------------
$uat = Join-Path $Engine 'Engine/Build/BatchFiles/RunUAT.bat'
$ubt = Join-Path $Engine 'Engine/Build/BatchFiles/Build.bat'
if (-not (Test-Path -LiteralPath $uat)) { Fail "no Unreal Engine at $Engine (pass -Engine or set UE_ROOT)" }
$engineBuild = Get-Content -LiteralPath (Join-Path $Engine 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
$engineVersion = "$($engineBuild.MajorVersion).$($engineBuild.MinorVersion).$($engineBuild.PatchVersion)"
if ("$($engineBuild.MajorVersion).$($engineBuild.MinorVersion)" -ne '5.8') { Fail "the engine at $Engine is $engineVersion; the project is 5.8" }
$gameIni = Get-Content -LiteralPath (Join-Path $repo 'Config/DefaultGame.ini') -Raw
$gameVersion = [regex]::Match($gameIni, '(?m)^ProjectVersion=(\S+)\s*$').Groups[1].Value
if (-not $gameVersion) { Fail 'no ProjectVersion in Config/DefaultGame.ini' }
$cookId = "$engineVersion-$gameVersion-$Revision"
$commit = ([string](git -C $repo rev-parse HEAD)).Trim()
$dirty = [bool](git -C $repo status --porcelain --untracked-files=no)
if ($dirty) { Write-Warning "the working tree has changes: the cook is not exactly commit $commit" }

# The game target (TargetType.Game) is the one UAT stages.
$target = Get-ChildItem -LiteralPath (Join-Path $repo 'Source') -Filter '*.Target.cs' |
    Where-Object { (Get-Content -LiteralPath $_.FullName -Raw) -match 'TargetType\.Game\b' } |
    ForEach-Object { $_.Name -replace '\.Target\.cs$', '' } | Select-Object -First 1
if (-not $target) { Fail 'no game target (TargetType.Game) in Source/' }

if (-not $Out) { $Out = Join-Path $repo 'Saved/MapPaks' }
$Out = [IO.Path]::GetFullPath($Out)
$cookOut = Join-Path $Out $cookId
$staging = Join-Path $Out 'Staged'
foreach ($stale in @($cookOut, $staging)) { if (Test-Path -LiteralPath $stale) { Remove-Item -LiteralPath $stale -Recurse -Force } }
New-Item -ItemType Directory -Force -Path $cookOut | Out-Null

Write-Host "Cooking $($Maps.Count) map(s) as cook $cookId ($commit$(if ($dirty) { ', with changes' })): $($Maps -join ', ')"
for ($i = 0; $i -lt $Maps.Count; ++$i) { Write-Host ("  pakchunk{0}  {1}" -f ($firstMapChunk + $i), $Maps[$i]) }

# --- Build ---------------------------------------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host 'Building the editor (the cook runs it)...'
    Invoke-Logged 'Build (editor)' (Join-Path $cookOut 'build-editor.log') { & $ubt OpenSCCTEditor Win64 Development "-project=$project" -WaitMutex }
    Write-Host "Building the game ($target $Configuration, which staging needs)..."
    Invoke-Logged 'Build (game)' (Join-Path $cookOut 'build-game.log') { & $ubt $target Win64 $Configuration "-project=$project" -WaitMutex }
}

# --- Cook, stage, pak ----------------------------------------------------------------------------------------------
$uatLog = Join-Path $cookOut 'uat.log'
Write-Host "Cooking and paking... this takes a while; the log is $uatLog"
$started = Get-Date
$uatArgs = @(
    'BuildCookRun', "-project=$project", '-noP4', '-utf8output', '-unattended',
    '-platform=Win64', "-clientconfig=$Configuration", "-target=$target", '-nocompileeditor',
    '-cook', '-stage', '-pak', '-iostore', '-compressed', '-manifests', '-CustomConfig=MapPaks',
    "-stagingdirectory=$staging",
    # UAT does not hand -CustomConfig to the cooker itself.
    "-AdditionalCookerOptions=-CustomConfig=MapPaks -ScctMapPaks=$($Maps -join '+')"
)
& $uat @uatArgs *> $uatLog
if ($LASTEXITCODE -ne 0) { Fail "BuildCookRun failed (exit code $LASTEXITCODE; see $uatLog)" }
Write-Host ('Cooked in {0:0} min' -f ((Get-Date) - $started).TotalMinutes)

# --- Collect the containers ----------------------------------------------------------------------------------------
$paks = Join-Path $staging "Windows/$projectName/Content/Paks"
if (-not (Test-Path -LiteralPath $paks)) { Fail "staging has no $paks (see $uatLog)" }
$chunkLists = Get-ChildItem -LiteralPath (Join-Path $repo 'Saved') -Recurse -Directory -Filter 'ChunkManifest' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '[\\/]Cooked[\\/]Windows[\\/]' } | Select-Object -First 1

function Get-Pack([int]$chunk, [string]$folder) {
    $name = "pakchunk$chunk-Windows"
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $files = foreach ($extension in 'pak', 'utoc', 'ucas') {
        $source = Join-Path $paks "$name.$extension"
        if (-not (Test-Path -LiteralPath $source)) { Fail "the cook made no $name.$extension (see $uatLog)" }
        Copy-Item -LiteralPath $source -Destination $folder
        $file = Get-Item -LiteralPath (Join-Path $folder "$name.$extension")
        [ordered]@{ name = $file.Name; size = $file.Length; sha256 = (Get-Sha256 $file.FullName) }
    }
    $packages = @()
    if ($chunkLists) {
        $list = Join-Path $chunkLists.FullName "pakchunk$chunk.txt"
        if (Test-Path -LiteralPath $list) {
            New-Item -ItemType Directory -Force -Path (Join-Path $cookOut 'chunks') | Out-Null
            Copy-Item -LiteralPath $list -Destination (Join-Path $cookOut "chunks/pakchunk$chunk.txt")
            $packages = @(Get-Content -LiteralPath $list | Where-Object { $_ })
        }
    }
    [ordered]@{ chunk = $chunk; container = $name; packageFiles = $packages.Count; files = @($files); lines = $packages }
}

$shared = Get-Pack $sharedChunk (Join-Path $cookOut "shared/$sharedId")
$mapPacks = for ($i = 0; $i -lt $Maps.Count; ++$i) {
    $code = $Maps[$i]
    $pack = Get-Pack ($firstMapChunk + $i) (Join-Path $cookOut "maps/$code")
    # The level must be in its own map's container, and nothing of another map's folder may be.
    if ($pack.lines.Count -gt 0) {
        # The cook's lists name each package by its cooked path without an extension.
        if (-not ($pack.lines | Where-Object { $_ -match "[\\/]Imported[\\/]Maps[\\/]$([regex]::Escape($code))[\\/]$([regex]::Escape($code))(\.umap)?$" })) {
            Fail "pakchunk$($pack.chunk) has no $code.umap (see $cookOut\chunks)"
        }
        # Besides the map's packages: the container's own shader library (ShaderArchive-/ShaderTypeInfo-..._Chunk<N>-...).
        $foreign = @($pack.lines | Where-Object { $_ -notmatch "[\\/]Imported[\\/]Maps[\\/]$([regex]::Escape($code))[\\/]" -and
            $_ -notmatch "[\\/]Shader(Archive|TypeInfo)-[^\\/]*_Chunk$($pack.chunk)-[^\\/]*$" })
        if ($foreign.Count -gt 0) { Fail "pakchunk$($pack.chunk) ($code) holds files outside its map's folder: $($foreign | Select-Object -First 5)" }
    }
    [ordered]@{ code = $code; level = "/Game/Imported/Maps/$code/$code"; requires = @($sharedId) } + $pack
}

# The game's container must hold nothing imported: that is what the packs are for.
if ($chunkLists) {
    $chunk0 = Join-Path $chunkLists.FullName 'pakchunk0.txt'
    if (Test-Path -LiteralPath $chunk0) {
        $leaked = @(Select-String -LiteralPath $chunk0 -Pattern '[\\/]Imported[\\/]' | ForEach-Object Line)
        if ($leaked.Count -gt 0) { Fail "pakchunk0 (the game) holds imported content: $($leaked | Select-Object -First 5)" }
    }
} else {
    Write-Warning 'the cook wrote no chunk lists (Metadata/ChunkManifest): the containers were not checked'
}
$extra = @(Get-ChildItem -LiteralPath $paks -Filter 'pakchunk*-Windows.utoc' | ForEach-Object { [int]($_.BaseName -replace '^pakchunk(\d+)-Windows$', '$1') } |
    Where-Object { $_ -ne 0 -and $_ -ne $sharedChunk -and ($_ -lt $firstMapChunk -or $_ -ge $firstMapChunk + $Maps.Count) })
if ($extra.Count -gt 0) { Fail "the cook made containers nobody asked for: pakchunk$($extra -join ', pakchunk')" }

# --- cook.json -----------------------------------------------------------------------------------------------------
function Strip($pack) { $copy = [ordered]@{}; foreach ($key in $pack.Keys) { if ($key -ne 'lines') { $copy[$key] = $pack[$key] } }; $copy }
$info = [ordered]@{
    format   = 'openscct.unreal-cook'
    formatVersion = 1
    cook     = [ordered]@{ id = $cookId; engine = $engineVersion; game = $gameVersion; commit = $commit; dirty = $dirty; platform = 'Windows' }
    built    = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    shared   = @([ordered]@{ id = $sharedId; title = 'Shared imported content' } + (Strip $shared))
    maps     = @($mapPacks | ForEach-Object { Strip $_ })
}
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $cookOut 'cook.json'), ($info | ConvertTo-Json -Depth 6) + "`n", $utf8)

# --- Report --------------------------------------------------------------------------------------------------------
Write-Host ''
Write-Host "Cook $cookId in $cookOut"
function Write-Pack([string]$label, $pack) {
    $total = 0; foreach ($file in $pack.files) { $total += $file.size }
    Write-Host ('  {0,-18} {1,-22} {2,8:0.0} MB  ({3} package files)' -f $label, $pack.container, ($total / 1MB), $pack.packageFiles)
}
Write-Pack $sharedId $shared
foreach ($pack in $mapPacks) { Write-Pack $pack.code $pack }
