<#
.SYNOPSIS
Install AMD FSR 4.1.2 for a Windows Unreal Engine 5.8 build machine.
.EXAMPLE
./Tools/Install-AmdFsr.ps1 -EngineRoot F:/UE_5.8
.EXAMPLE
./Tools/Install-AmdFsr.ps1 -EngineRoot F:/UE_5.8 -Archive D:/Downloads/UE-FSR-4_1_2.zip
.NOTES
The default download is about 2.7 GB. The UE 5.8 plugin includes AMD's license,
sources, shaders, precompiled modules, and runtime libraries. Git and the UE Win64 C++ toolchain
are needed to rebuild the frame-interpolation module for windows without a presentation swapchain.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [string]$Archive
)

$ErrorActionPreference = 'Stop'
$download = 'https://download.amd.com/gpuopen/UE-FSR-4_1_2.zip'
$prefix = 'UE-FSR-4_1_2/FSR-580/'
$engine = (Get-Item -LiteralPath $EngineRoot).FullName.TrimEnd('\', '/')
$version = Get-Content -Raw -LiteralPath (Join-Path $engine 'Engine/Build/Build.version') | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8) { throw 'AMD FSR 4.1.2 requires Unreal Engine 5.8.' }
$engineId = (Get-Content -Raw -LiteralPath (Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.modules') | ConvertFrom-Json).BuildId
$marketplace = Join-Path $engine 'Engine/Plugins/Marketplace'
$target = Join-Path $marketplace 'FSR'
$patch = Join-Path $PSScriptRoot 'AmdFsrViewport.patch'
$receiptName = '.openscct-fsr-build.json'
$pristineDigest = '4FB42CF1FC54F7465BCC49B2DB32687B8C4D6ADDF4CDE79139A96DF2A5EE48BE'

function Confirm-EngineBuild([string]$Folder) {
    $modules = Get-Content -Raw -LiteralPath (Join-Path $Folder 'Binaries/Win64/UnrealEditor.modules') | ConvertFrom-Json
    if (!$engineId -or $modules.BuildId -ne $engineId) {
        throw "FSR's precompiled modules require engine BuildId $($modules.BuildId). This engine is $engineId."
    }

}

function Get-PayloadDigest([string]$Folder) {
    # Fingerprint the complete payload, including both copies of the AMD SDK DLLs.
    # Use relative paths and ordinal order so the result is independent of location and locale.
    $base = [IO.Path]::GetFullPath($Folder).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    [string[]]$names = foreach ($file in [IO.Directory]::EnumerateFiles($base, '*', [IO.SearchOption]::AllDirectories)) {
        $relative = $file.Substring($base.Length).Replace('\', '/')
        if ([IO.Path]::GetExtension($file) -ne '.pdb' -and $relative -cne $receiptName) { $relative }
    }
    [Array]::Sort($names, [StringComparer]::Ordinal)
    $list = [Text.StringBuilder]::new()
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($name in $names) {
            $stream = [IO.File]::OpenRead((Join-Path $base $name))
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
            finally { $stream.Dispose() }
            [void]$list.Append("$hash *$name`n")
        }
        $digest = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($list.ToString()))).Replace('-', '')
    } finally { $sha.Dispose() }
    return $digest
}

function Build-ViewportFix([string]$Folder, [string]$Workspace) {
    $hostDir = Join-Path $Workspace 'Host'
    $hostPlugin = Join-Path $hostDir 'Plugins/FSR'
    [IO.Directory]::CreateDirectory($hostPlugin) | Out-Null
    Get-ChildItem -LiteralPath $Folder | Where-Object Name -in 'Binaries','Resources','Shaders','Source','FSR.uplugin' |
        Copy-Item -Destination $hostPlugin -Recurse
    $project = Join-Path $hostDir 'Host.uproject'
    [IO.File]::WriteAllText($project, '{"FileVersion":3,"Plugins":[{"Name":"FSR","Enabled":true}]}')
    $unit = 'Source/FFXFrameInterpolation/Private/FFXFrameInterpolation.cpp'
    & git -C $hostPlugin apply --ignore-space-change $patch
    if ($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath (Join-Path $hostPlugin $unit) -SimpleMatch 'if (!ViewportRHI)')) {
        throw 'Could not apply the viewport lifetime fix to the AMD source.'
    }

    foreach ($build in @('UnrealEditor Development','UnrealGame Development','UnrealGame Shipping')) {
        $kind, $config = $build.Split(' ')
        & (Join-Path $engine 'Engine/Build/BatchFiles/Build.bat') $kind Win64 $config "-Project=$project" "-Plugin=$hostPlugin/FSR.uplugin" -NoUBTMakefiles -NoHotReload -WaitMutex
        if ($LASTEXITCODE -ne 0) { throw "Failed to compile AMD's $build module." }
    }
    Confirm-EngineBuild $hostPlugin
    foreach ($file in @($unit,'Binaries/Win64/UnrealEditor-FFXFrameInterpolation.dll',
            'Intermediate/Build/Win64/x64/UnrealEditor/Development/FFXFrameInterpolation/UnrealEditor-FFXFrameInterpolation.lib')) {
        Copy-Item -LiteralPath (Join-Path $hostPlugin $file) -Destination (Join-Path $Folder $file) -Force
    }
    foreach ($config in @('Development','Shipping')) {
        $relative = "Intermediate/Build/Win64/x64/UnrealGame/$config/FFXFrameInterpolation"
        $from = Join-Path $hostPlugin $relative
        $to = Join-Path $Folder $relative
        $manifestName = 'FFXFrameInterpolation.precompiled'
        $objects = (Get-Content -LiteralPath (Join-Path $from $manifestName) -Raw | ConvertFrom-Json).OutputFiles
        if (!$objects.Count) { throw "Empty AMD $config precompiled module." }
        Get-ChildItem -LiteralPath $to -File -Filter '*.obj' | Remove-Item -Force
        foreach ($name in (@($objects) + $manifestName)) {
            if ([IO.Path]::GetFileName($name) -cne $name) { throw "Unexpected build product $name." }
            Copy-Item -LiteralPath (Join-Path $from $name) -Destination (Join-Path $to $name) -Force
        }
    }
}

# Use a normalized source-patch fingerprint across Git LF/CRLF checkout styles.
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $patchText = [IO.File]::ReadAllText($patch).Replace("`r`n", "`n")
    $patchDigest = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($patchText))).Replace('-', '')
} finally { $sha.Dispose() }
$existing = Test-Path -LiteralPath $target
if ($existing) {
    Confirm-EngineBuild $target
    $digest = Get-PayloadDigest $target
    $receipt = Join-Path $target $receiptName
    if (Test-Path -LiteralPath $receipt) {
        $record = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
        if ($record.Payload -eq $digest -and $record.Upstream -eq $pristineDigest -and
            $record.Patch -eq $patchDigest -and $record.Engine -eq $engineId) {
            Write-Output "AMD FSR 4.1.2 with the viewport fix is already installed in $target."
            return
        }
        throw "The existing FSR build at $target differs from its recorded build. Installation stopped."
    }
    if ($digest -ne $pristineDigest) { throw "The existing FSR installation at $target differs from AMD's package." }
}

$scratch = Join-Path $engine ('.scct-fsr-' + [Guid]::NewGuid().ToString('N'))
$staged = Join-Path $scratch 'FSR'
[IO.Directory]::CreateDirectory($staged) | Out-Null
$published = $false
try {
    if ($existing) {
        Get-ChildItem -LiteralPath $target -Force | Copy-Item -Destination $staged -Recurse
    } else {
        if ($PSBoundParameters.ContainsKey('Archive')) {
            $package = (Get-Item -LiteralPath $Archive).FullName
        } else {
            $package = Join-Path $scratch 'UE-FSR-4_1_2.zip'
            $ProgressPreference = 'SilentlyContinue'
            Invoke-WebRequest -UseBasicParsing -Uri $download -OutFile $package
        }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($package)
        try {
            $inside = [IO.Path]::GetFullPath($staged) + [IO.Path]::DirectorySeparatorChar
            foreach ($entry in $zip.Entries) {
                if (!$entry.Name -or !$entry.FullName.StartsWith($prefix, [StringComparison]::Ordinal)) { continue }
                $relative = $entry.FullName.Substring($prefix.Length)
                if ($relative.StartsWith('FSR/', [StringComparison]::Ordinal)) { $relative = $relative.Substring(4) }
                elseif ($relative -cnotin @('LICENSE.txt', 'readme.md')) { continue }
                if ([IO.Path]::GetExtension($relative) -eq '.pdb') { continue }
                $file = [IO.Path]::GetFullPath((Join-Path $staged $relative))
                if (!$file.StartsWith($inside, [StringComparison]::OrdinalIgnoreCase)) { throw "Invalid archive entry $relative." }
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file)) | Out-Null
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $file)
            }
        } finally { $zip.Dispose() }
    }
    Confirm-EngineBuild $staged
    if ((Get-PayloadDigest $staged) -ne $pristineDigest) { throw "Package contents differ from AMD's UE 5.8 FSR 4.1.2 release." }
    Build-ViewportFix $staged $scratch
    $payload = Get-PayloadDigest $staged
    if ($payload -eq $pristineDigest) { throw 'The FSR rebuild did not update the plugin.' }
    [IO.File]::WriteAllText((Join-Path $staged $receiptName), (@{
        Engine=$engineId; Upstream=$pristineDigest; Patch=$patchDigest; Payload=$payload
    } | ConvertTo-Json))
    [IO.Directory]::CreateDirectory($marketplace) | Out-Null
    # The staging directory and Marketplace are on the same volume. Directory.Move also refuses an existing target.
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($target)) -ne [IO.Path]::GetFullPath($marketplace) -or
        [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($staged)) -ne [IO.Path]::GetFullPath($scratch)) {
        throw 'Unexpected plugin installation path.'
    }
    $previous = Join-Path $scratch 'Original'
    if ($existing) {
        if ((Get-PayloadDigest $target) -ne $pristineDigest -or [IO.Path]::GetDirectoryName($previous) -ne $scratch) {
            throw 'The existing FSR installation changed while the patch was building.'
        }
        [IO.Directory]::Move($target, $previous)
    }
    try {
        [IO.Directory]::Move($staged, $target)
        $published = $true
    }
    catch {
        if ($existing) { [IO.Directory]::Move($previous, $target) }
        throw
    }
} finally {
    $cleanup = [IO.Path]::GetFullPath($scratch)
    if ([IO.Path]::GetDirectoryName($cleanup) -eq $engine -and [IO.Path]::GetFileName($cleanup) -match '^\.scct-fsr-[0-9a-f]{32}$' -and
        ($published -or !(Test-Path -LiteralPath (Join-Path $cleanup 'Original')))) {
        Remove-Item -LiteralPath $cleanup -Recurse -Force
    }
}
Write-Output "Installed AMD FSR 4.1.2 with the viewport fix in $target."
