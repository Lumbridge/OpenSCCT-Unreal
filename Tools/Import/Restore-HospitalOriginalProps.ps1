param(
    [Parameter(Mandatory)] [string] $OriginalDirectory,
    [Parameter(Mandatory)] [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
$originals = (Resolve-Path -LiteralPath $OriginalDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if ($output -eq $originals -or $output.StartsWith($originals + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Restore the textures into a staging folder outside the original map.'
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
function Invoke-ImageMagick([string[]] $Options) {
    & magick @Options
    if ($LASTEXITCODE -ne 0) { throw 'ImageMagick failed to restore a texture' }
}
function Restore-Atlas([string] $Name, [string] $FineEdge, [string] $BroadEdge) {
    $source = Join-Path $originals $Name
    $final = Join-Path $output $Name
    $resize = '2048x2048!'
    $stem = [IO.Path]::GetFileNameWithoutExtension($Name)
    $image = Join-Path $output "$stem-stage.miff"
    $corrected = Join-Path $output "$stem-corrected.miff"
    $temporary = Join-Path $output "$stem-ready.png"
    $dims = & magick identify -format '%wx%h' $source
    if ($LASTEXITCODE -ne 0 -or $dims -ne '256x256') { throw "Unexpected original atlas size: $Name" }
    try {
        # Band-limited upsampling and two local-contrast radii sharpen source edges only.
        Invoke-ImageMagick @($source, '-alpha','off', '-filter','LanczosSharp', '-resize',$resize,
            '-unsharp',$FineEdge, '-unsharp',$BroadEdge, $image)
        # Keep source illumination and color anchored when the restored image is sampled at the original resolution.
        # Center the correction at half range to carry signed values through the intermediate.
        for ($pass = 0; $pass -lt 2; ++$pass) {
            Invoke-ImageMagick @($image, '(', $image, '-scale','256x256!', $source, '-alpha','off',
                '-define','compose:args=0,1,-1,0.5', '-compose','Mathematics', '-composite',
                '-filter','Triangle', '-resize',$resize, ')', '-define','compose:args=0,1,1,-0.5',
                '-compose','Mathematics', '-composite', $corrected)
            Move-Item -Force -LiteralPath $corrected -Destination $image
        }
        # Preserve the authored alpha samples independently of the RGB reconstruction.
        Invoke-ImageMagick @($image, '(', $source, '-alpha','extract', '-sample',$resize, ')',
            '-compose','CopyAlpha', '-composite', '-depth','8', '-define','png:color-type=6', $temporary)
        Move-Item -Force -LiteralPath $temporary -Destination $final
    } finally {
        foreach ($restorationTemp in @($image,$corrected,$temporary)) {
            if (Test-Path -LiteralPath $restorationTemp) { Remove-Item -LiteralPath $restorationTemp }
        }
    }
}

$vendor = 'Prison_TEX_machine.png'
$vendorEmf = 'Prison_TEX_machineEMF.png'
$medical = 'Prison_TEX_Box1.png'
if ((Get-FileHash -LiteralPath (Join-Path $originals $vendor)).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $originals $vendorEmf)).Hash) {
    throw 'The two vending aliases no longer have identical source data.'
}
$vendingAtlas = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Art/Maps/MounE/Prison_TEX_machine_HD.png'))
if (-not (Test-Path -LiteralPath $vendingAtlas -PathType Leaf)) {
    throw "The HD Mount Hospital restoration requires the approved local vending cabinet atlas at $vendingAtlas"
}
Copy-Item -LiteralPath $vendingAtlas -Destination (Join-Path $output $vendor) -Force
Restore-Atlas $medical '0x2.2+0.73+0.012' '0x5.1+0.28+0.009'
[ordered]@{ $medical = $medical; $vendor = $vendor; $vendorEmf = $vendor } |
    ConvertTo-Json | Set-Content -Encoding utf8 -LiteralPath (Join-Path $output 'manifest.json')
