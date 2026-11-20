<#
.SYNOPSIS
Install the official NVIDIA DLSS 4.5 plugins for a Windows UE 5.8 build machine.
.EXAMPLE
./Tools/Install-NvidiaDlss.ps1 -EngineRoot F:/UE_5.8
.NOTES
An existing download can be supplied with -Archive. The SDK license is in Documentation/NVIDIA RTX SDK EULA.pdf
inside the archive. The engine plugins stage their redistributable runtime files when packaging the game.
Keep development tools and the SDK archive out of game distributions. See the SDK license for end-user terms,
credits, marks and the prior commercial release notification at https://developer.nvidia.com/sw-notification.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [string]$Archive = (Join-Path ([IO.Path]::GetTempPath()) 'NVIDIA-DLSS-UE5.8-8.9.0.zip')
)

$ErrorActionPreference = 'Stop'
$release = '8.9.0'
$download = 'https://developer.nvidia.com/downloads/assets/gameworks/downloads/secure/dlss/ue-dlss-5.8/8.9.0/2026.10.16_ue5.8_dlss4.5plugin_v8.9.0.zip'
$digest = 'adeeb59b9635794e05252438bf01307bf8a629769e8f0517237363f690bb380c'
$plugins = @('DLSS', 'StreamlineCore', 'StreamlineDLSSG', 'StreamlineNGXCommon', 'StreamlineReflex')
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$version = Get-Content -LiteralPath (Join-Path $engine 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8) { throw 'This SDK requires Unreal Engine 5.8.' }
$engineId = (Get-Content -LiteralPath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json).BuildId
$destination = [IO.Path]::GetFullPath((Join-Path $engine 'Engine/Plugins/Marketplace'))
[IO.Directory]::CreateDirectory($destination) | Out-Null
$pending = foreach ($name in $plugins) {
    $folder = Join-Path $destination $name
    if (!(Test-Path -LiteralPath $folder)) { $name; continue }
    $descriptor = Get-Content -LiteralPath (Join-Path $folder "$name.uplugin") -Raw | ConvertFrom-Json
    $binary = Get-Content -LiteralPath (Join-Path $folder 'Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json
    if (($descriptor.VersionName -split '-')[0] -ne $release -or $binary.BuildId -ne $engineId) {
        throw "A different version is installed at $folder. Keep or move that SDK before installing this one."
    }
}
if (!$pending) { Write-Output "NVIDIA DLSS $release is already installed in $destination"; return }
if (!(Test-Path -LiteralPath $Archive)) {
    Invoke-WebRequest -Uri $download -OutFile $Archive
}
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $digest) {
    throw "The SHA-256 of $Archive does not match NVIDIA's pinned UE 5.8 archive."
}

# Extract outside UE's plugin search path. Publish each folder only after its files and build ID are verified.
$scratch = Join-Path $engine ('.scct-dlss-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($scratch) | Out-Null
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Archive).Path)
    try {
        foreach ($entry in $zip.Entries) {
            if (!$entry.Name -or $entry.FullName -notmatch '^Plugins/([^/]+)/(.+)$' -or $Matches[1] -notin $pending) { continue }
            $file = [IO.Path]::GetFullPath((Join-Path $scratch ($Matches[1] + '/' + $Matches[2])))
            if (!$file.StartsWith($scratch + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Invalid archive entry: $($entry.FullName)"
            }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file)) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $file)
        }
    } finally { $zip.Dispose() }
    foreach ($name in $pending) {
        $folder = Join-Path $scratch $name
        $binary = Get-Content -LiteralPath (Join-Path $folder 'Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json
        if ($binary.BuildId -ne $engineId) { throw "NVIDIA's $name binaries do not match engine BuildId $engineId." }
    }
    foreach ($name in $pending) {
        $source = [IO.Path]::GetFullPath((Join-Path $scratch $name))
        $target = [IO.Path]::GetFullPath((Join-Path $destination $name))
        if ([IO.Path]::GetDirectoryName($source) -ne $scratch -or [IO.Path]::GetDirectoryName($target) -ne $destination) {
            throw 'Plugin path is outside its installation directory.'
        }
        [IO.Directory]::Move($source, $target)
    }
} finally {
    if ([IO.Path]::GetDirectoryName($scratch) -eq $engine -and [IO.Path]::GetFileName($scratch) -match '^\.scct-dlss-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
Write-Output "Installed NVIDIA DLSS $release in $destination"
