<#
.SYNOPSIS
Rebuilds the merc rifle from its scripts: model, textures, renders and the FBX files Unreal imports. Blender runs
in the background only (no window).

.DESCRIPTION
  bake    merc_rifle.py models the rifle, unwraps it and bakes the geometry buffers (Saved\MercRifle, not committed)
  paint   paint_textures.py paints the PBR textures from those buffers (textures\)
  finish  merc_rifle.py puts the textures on, rigs the rifle, renders it (renders\) and exports MercRifle.fbx,
          MercRifle_LOD1.fbx and MercRifle.blend

.EXAMPLE
Art\Weapons\MercRifle\Build-MercRifle.ps1
Art\Weapons\MercRifle\Build-MercRifle.ps1 -Stages paint,finish -Views three_quarter,first_person
#>
param(
    [string[]] $Stages = @('bake', 'paint', 'finish'),
    # Which beauty shots the finish stage renders (all when empty); see VIEWS in merc_rifle.py.
    [string[]] $Views = @(),
    [string] $Blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$work = Join-Path $project 'Saved\MercRifle'
New-Item -ItemType Directory -Force $work | Out-Null

function Invoke-Blender([string] $stage, [string] $extra) {
    $arguments = @('--background', '--factory-startup', '--python', "$PSScriptRoot\merc_rifle.py", '--', $stage, $work)
    if ($extra) { $arguments += $extra }
    & $Blender @arguments 2>&1 | Select-String -Pattern 'MERC-RIFLE|Traceback|Error:|File "|line \d+' | ForEach-Object { $_.Line }
    if ($LASTEXITCODE -ne 0) { throw "Blender stage '$stage' failed." }
}

foreach ($stage in $Stages) {
    switch ($stage) {
        'bake' { Invoke-Blender 'bake' '' }
        'paint' {
            # The lettering uses the open font that ships with Blender.
            $font = Get-ChildItem (Split-Path $Blender) -Directory | Where-Object Name -Match '^\d+\.\d+$' |
                ForEach-Object { Join-Path $_.FullName 'datafiles\fonts\Inter.woff2' } | Select-Object -First 1
            python "$PSScriptRoot\paint_textures.py" $work $font
            if ($LASTEXITCODE -ne 0) { throw 'Texture painting failed.' }
        }
        'finish' { Invoke-Blender 'finish' ($Views -join ',') }
        default { throw "Unknown stage '$stage'." }
    }
}
