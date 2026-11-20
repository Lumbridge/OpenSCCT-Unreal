#Requires -Version 7.3
<#
.SYNOPSIS
Checks the Unreal server's content selection against a conflicting installation and an explicitly configured map.
.EXAMPLE
pwsh Tools/Test/Test-DedicatedContent.ps1 -Archive Saved/Release/0.1.11/OpenSCCT-Unreal-Server-0.1.11-win-x64.zip
#>
param([Parameter(Mandatory)][string]$Archive)
$ErrorActionPreference = 'Stop'
$artifact = (Resolve-Path -LiteralPath $Archive).Path
$work = [IO.Directory]::CreateTempSubdirectory('unreal-dedicated-content-').FullName
$install = Join-Path $work 'install'

function Test-Server([hashtable]$config, [int]$expectedCode, [string]$expectedText) {
    $settings = Join-Path $work 'server.json'
    $config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $settings
    $processInfo = [Diagnostics.ProcessStartInfo]::new($script:program)
    $processInfo.WorkingDirectory = $work
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    foreach ($arg in @('--config', $settings, '--seconds', '1')) { $processInfo.ArgumentList.Add($arg) }
    $processInfo.Environment['OPENSCCT_UNREAL_CONTENT_DIR'] = Join-Path $work 'unreal-content'
    $processInfo.Environment['OPENSCCT_GODOT_OPTIONAL_MAPS'] = $script:foreignMaps
    $processInfo.Environment['SVM_CONTENT'] = Split-Path -Parent $script:foreignMaps
    $processInfo.Environment['OPENSCCT_PACKS'] = Split-Path -Parent $script:foreignMaps
    $process = [Diagnostics.Process]::Start($processInfo)
    try {
        $out = $process.StandardOutput.ReadToEndAsync()
        $err = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true)
            throw 'Dedicated server content test timed out.'
        }
        $log = $out.Result + $err.Result
        if ($process.ExitCode -ne $expectedCode -or !$log.Contains($expectedText, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected dedicated server result ($($process.ExitCode)): $log"
        }
    } finally { $process.Dispose() }
}

try {
    if ($IsWindows) {
        [IO.Compression.ZipFile]::ExtractToDirectory($artifact, $install)
        $program = Join-Path $install 'svm-server.exe'
    } else {
        [void][IO.Directory]::CreateDirectory($install)
        tar -xzf $artifact -C $install
        if ($LASTEXITCODE -ne 0) { throw 'Could not extract the Linux server.' }
        $program = Join-Path $install 'svm-server'
    }
    $foreignMaps = [IO.Directory]::CreateDirectory((Join-Path $work 'other-edition/svmap')).FullName
    $map = Join-Path $foreignMaps 'Hub'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../../Runtime/fixtures/hub') -Destination $map -Recurse
    $config = @{ port=0; lanPort=0; loopbackOnly=$true; rotation=@(@{map='Hub'; mode='Story'}) }
    Test-Server $config 3 "Rotation map 'Hub' is not installed"
    $config['maps'] = $foreignMaps
    Test-Server $config 0 'Hub'
    $generation = Join-Path $work 'wrong-recipe/steam-builds/11111111111111111111111111111111'
    $config['maps'] = [IO.Directory]::CreateDirectory((Join-Path $generation 'svmap')).FullName
    Copy-Item -LiteralPath $map -Destination $config['maps'] -Recurse
    Test-Server $config 3 'different or incomplete content build'
    Write-Output 'Passed foreign content isolation, explicit map selection and mismatched Unreal generation checks.'
} finally {
    $actual = [IO.Path]::GetFullPath($work)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ([IO.Path]::GetDirectoryName($actual) -eq $tempRoot -and [IO.Path]::GetFileName($actual).StartsWith('unreal-dedicated-content-')) {
        Remove-Item -LiteralPath $actual -Recurse -Force
    }
}
