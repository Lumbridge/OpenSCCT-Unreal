#Requires -Version 7.3
<#
.SYNOPSIS
Builds the self-contained OpenSCCT Unreal dedicated server using the runtime in this checkout.
.EXAMPLE
pwsh Tools/Package-DedicatedServer.ps1 -Version 0.1.10 -Out Saved/Release/0.1.10
#>
[CmdletBinding()]
param(
    [string]$Version,
    [ValidateSet('linux-x64', 'win-x64')]
    [string[]]$RuntimeIdentifier = @('linux-x64', 'win-x64'),
    [string]$Out
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $repo 'Runtime'
$project = Join-Path $runtimeRoot 'src/OpenScct.Server.Cli/OpenScct.Server.Cli.csproj'
$templates = Join-Path $repo 'Deployment/Dedicated'
if (-not $Version) {
    $ini = Get-Content -Raw -LiteralPath (Join-Path $repo 'Config/DefaultGame.ini')
    $Version = [regex]::Match($ini, '(?m)^ProjectVersion=([^\r\n]+)').Groups[1].Value.Trim()
}
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[a-zA-Z0-9.-]+)?$') { throw "Invalid Unreal release version '$Version'." }
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) { throw "This checkout is missing its dedicated server source: $project" }
if (-not $Out) { $Out = Join-Path $repo "Saved/Release/$Version" }
$destination = [IO.Directory]::CreateDirectory($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)).FullName
$work = [IO.Directory]::CreateTempSubdirectory('openscct-unreal-server-').FullName

function Write-LinuxArchive([string]$directory, [string]$archive) {
    $stream = [IO.File]::Create($archive)
    try {
        $gzip = [IO.Compression.GZipStream]::new($stream, [IO.Compression.CompressionLevel]::Optimal, $true)
        try {
            $tar = [System.Formats.Tar.TarWriter]::new($gzip, $true)
            try {
                foreach ($file in (Get-ChildItem -LiteralPath $directory -File -Recurse | Sort-Object FullName)) {
                    $relative = [IO.Path]::GetRelativePath($directory, $file.FullName).Replace('\', '/')
                    $entry = [System.Formats.Tar.PaxTarEntry]::new([System.Formats.Tar.TarEntryType]::RegularFile, $relative)
                    # Set Linux launch permissions even when the package was built on Windows.
                    $entry.Mode = if ($relative -in @('svm-server', 'createdump')) { [IO.UnixFileMode]493 } else { [IO.UnixFileMode]420 }
                    $entry.ModificationTime = $file.LastWriteTimeUtc
                    $inputStream = $file.OpenRead()
                    try {
                        $entry.DataStream = $inputStream
                        $tar.WriteEntry($entry)
                    } finally { $inputStream.Dispose() }
                }
            } finally { $tar.Dispose() }
        } finally { $gzip.Dispose() }
    } finally { $stream.Dispose() }
}

try {
    $receipt = Join-Path $work 'contenttools'
    python -B (Join-Path $repo 'Tools/UnrealContent/package.py') $receipt --receipt-only
    if ($LASTEXITCODE -ne 0) { throw 'Could not generate the Unreal content recipe for the dedicated server.' }
    foreach ($rid in ($RuntimeIdentifier | Select-Object -Unique)) {
        $publish = Join-Path $work $rid
        Push-Location $runtimeRoot
        try {
            dotnet publish $project -c Release -r $rid --self-contained true -o $publish -nologo -v quiet
            if ($LASTEXITCODE -ne 0) { throw "Dedicated server publish for $rid failed ($LASTEXITCODE)." }
        } finally { Pop-Location }

        # Use the same authored TestBox manifest and collision that the Unreal bridge ships.
        $maps = [IO.Directory]::CreateDirectory((Join-Path $publish 'maps')).FullName
        Copy-Item -LiteralPath (Join-Path $runtimeRoot 'fixtures/testbox') -Destination (Join-Path $maps 'TestBox') -Recurse
        Copy-Item -LiteralPath $receipt -Destination (Join-Path $publish 'contenttools') -Recurse
        Copy-Item -LiteralPath (Join-Path $templates 'server.json'), (Join-Path $templates 'README.md') -Destination $publish
        if ($rid -eq 'linux-x64') {
            Copy-Item -LiteralPath (Join-Path $templates 'openscct-unreal-server.service') -Destination $publish
            $name = "OpenSCCT-Unreal-Server-$Version-$rid.tar.gz"
            Write-LinuxArchive $publish (Join-Path $work $name)
        } else {
            $name = "OpenSCCT-Unreal-Server-$Version-$rid.zip"
            [IO.Compression.ZipFile]::CreateFromDirectory($publish, (Join-Path $work $name))
        }
        $archive = Join-Path $destination $name
        $digest = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $work $name)).Hash.ToLowerInvariant()
        Move-Item -LiteralPath (Join-Path $work $name) -Destination $archive -Force
        [IO.File]::WriteAllText("$archive.sha256", "$digest  $name`n")
        Write-Output $archive
    }
} finally {
    $actual = [IO.Path]::GetFullPath($work)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ([IO.Path]::GetDirectoryName($actual) -eq $tempRoot -and [IO.Path]::GetFileName($actual).StartsWith('openscct-unreal-server-')) {
        Remove-Item -LiteralPath $actual -Recurse -Force
    }
}
