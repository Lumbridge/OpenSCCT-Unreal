<#
.SYNOPSIS
Proves the game against a real OpenSCCT dedicated server, with nobody at the keyboard and no window.

.DESCRIPTION
Starts OpenSCCT's own dedicated server (src/OpenScct.Server.Cli of an OpenSCCT checkout, as Bridge/Probes/OnlineProbe
does: loopback only, a port of its own, bots filling the sides), then starts the game off-screen at its front end
with -ScctJoin so it connects, says ready, travels into the round and plays. Screenshots go to
Saved\ScctShots\<Tag>\ and the log to Saved\Logs\online-<Tag>.log (the server's output to online-<Tag>.server.txt).
Only the server process this script started is stopped at the end.

The server project must have been built once: dotnet build -c Release --no-dependencies src/OpenScct.Server.Cli

.EXAMPLE
Tools\Run-OnlineCheck.ps1 -Content C:\path\to\.content -OpenScct C:\path\to\OpenSCCT -Map AquaE -Side spy

.EXAMPLE
# The join made from the menus, as a player makes it.
Tools\Run-OnlineCheck.ps1 -Content C:\path\to\.content -OpenScct C:\path\to\OpenSCCT -Tag menujoin -MenuDo "wait 8; page address; wait 1; type 127.0.0.1:28961; press CONNECT; wait 10; shot lobby; press READY; wait 30; shot match; quit"

.EXAMPLE
# A join that cannot work: no server is started and the port is one nothing listens on.
Tools\Run-OnlineCheck.ps1 -Content C:\path\to\.content -NoServer -Port 28999 -Tag fail -Shots 3 -Every 6
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    [string] $OpenScct = '',
    [string] $Map = 'AquaE',
    [string] $SecondMap = 'ClubE',
    [ValidateSet('spy', 'merc', 'watch', '')] [string] $Side = 'spy',
    [int] $Port = 28961,
    # Minutes a round lasts on the server (its results and the way back to the lobby follow).
    [int] $TimeLimit = 10,
    # Minutes the second map's round lasts (0: the map's own setting). A personal demo downloads between rounds, so
    # a recording check (-Extra -ScctRecord) wants short rounds on both maps.
    [int] $SecondTimeLimit = 0,
    [int] $Shots = 6,
    [double] $Every = 6,
    # Seconds into the first round at which the game leaves the server (0: it stays).
    [double] $LeaveAfter = 0,
    # Seconds after the game was started at which the server is stopped under it (0: it runs to the end): the
    # game must come back to the front end and say why.
    [double] $KillServerAfter = 0,
    [string] $Tag = 'join',
    # Human players the server waits for before it starts a match on its own once they are ready (a number over
    # the players that join keeps it from starting by itself, for a look at the lobby's LAUNCH button).
    [int] $MinPlayers = 1,
    # Join without starting a server (the failure check).
    [switch] $NoServer,
    # Do not say ready: the game stays in the lobby (for a look at it).
    [switch] $StayInLobby,
    # A practice server (unlimited lives, the in-match menu's Switch Team).
    [switch] $Practice,
    # Join from the menus instead of -ScctJoin: a menu script (Source/OpenSCCT/Menu/ScctMenuDriver.h) that opens
    # the join-by-address page, types 127.0.0.1:<Port> and presses CONNECT, for example. Its shots go to the same folder.
    [string] $MenuDo = '',
    # The game's picture size (the lobby and the results are worth a look at 1920x1080 as well).
    [int] $ResX = 1280,
    [int] $ResY = 720,
    # Anything else for the game's command line, as written.
    [string] $Extra = '',
    # Run the audio engine without a sound device (-DeterministicAudio) instead of no audio engine (-nosound), so
    # voices really play (voice chat's playback, the submixes), into nothing.
    [switch] $NullAudio,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path "$PSScriptRoot\..").Path
$logs = Join-Path $project 'Saved\Logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$shotDir = (Join-Path $project "Saved\ScctShots\$Tag").Replace('\', '/')
$log = (Join-Path $logs "online-$Tag.log").Replace('\', '/')
$serverLog = Join-Path $logs "online-$Tag.server.txt"

$server = $null
$work = $null
try {
    if (-not $NoServer) {
        if (-not $OpenScct) { throw 'Pass -OpenScct <OpenSCCT checkout> (its src/OpenScct.Server.Cli built in Release).' }
        $work = Join-Path ([IO.Path]::GetTempPath()) "scct-online-check-$PID"
        New-Item -ItemType Directory -Force $work | Out-Null
        $config = Join-Path $work 'svm-server.json'
        # A dedicated server as an operator runs it: a config file and commands on standard input.
        @{
            name = 'Unreal online check'; port = $Port; lanPort = 0; loopbackOnly = $true; maxPlayers = 4
            maps = (Join-Path $Content 'svmap')
            rotation = @(@{ map = $Map; mode = 'Story'; timeLimit = $TimeLimit },
                $(if ($SecondTimeLimit -gt 0) { @{ map = $SecondMap; mode = 'Story'; timeLimit = $SecondTimeLimit } } else { @{ map = $SecondMap; mode = 'Story' } }))
            minPlayersToLaunch = $MinPlayers; launchCountdownSeconds = 2; resultsSeconds = 6; bansFile = $null; matchLog = $null
            practice = [bool] $Practice
        } | ConvertTo-Json -Depth 4 | Set-Content -Path $config -Encoding utf8

        # The server's output goes straight to a file (cmd's redirection), so nothing here has to keep reading it;
        # its standard input stays a pipe, for the console commands below.
        $start = [Diagnostics.ProcessStartInfo]::new('cmd.exe')
        $start.WorkingDirectory = $work
        $start.RedirectStandardInput = $true
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        # --no-build: the OpenSCCT projects are shared with other work and are never rebuilt from here.
        $cli = Join-Path $OpenScct 'src\OpenScct.Server.Cli'
        $start.Arguments = "/c dotnet run --no-build -c Release --project `"$cli`" -- --config `"$config`" > `"$serverLog`" 2>&1"
        $start.Environment['SVM_CONTENT'] = $Content
        Set-Content -Path $serverLog -Value ''
        $server = [Diagnostics.Process]::Start($start)

        # "on UDP port" is the server's sign of being up.
        $deadline = (Get-Date).AddSeconds(90)
        while (-not (Select-String -Path $serverLog -SimpleMatch 'on UDP port' -Quiet)) {
            if ($server.HasExited -or (Get-Date) -gt $deadline) { throw "The server did not come up (see $serverLog)." }
            Start-Sleep -Milliseconds 300
        }

        # The original has no bots in a network session; ours fill the empty places of both sides.
        $server.StandardInput.WriteLine('set bots fill')
        $server.StandardInput.WriteLine('set botskill normal')
        "server up on 127.0.0.1:$Port (process $($server.Id))"
    }

    $arguments = @(
        "`"$project\OpenSCCTUnreal.uproject`"",
        '-game', '-RenderOffscreen', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes', '-unattended', '-nosplash',
        $(if ($NullAudio) { '-DeterministicAudio' } else { '-nosound' }),
        "-ScctContent=`"$("$Content".Replace('\', '/'))`"",
        $(if ($MenuDo) { "-ScctMenuDo=`"$MenuDo`"" } else { "-ScctJoin=127.0.0.1:$Port" }), '-ScctName=UnrealCheck',
        $(if (-not $StayInLobby -and -not $MenuDo) { '-ScctAutoReady' }),
        $(if ($Side) { "-ScctSide=$Side" }),
        $(if ($LeaveAfter -gt 0) { "-ScctLeaveAfter=$LeaveAfter" }),
        "-ScctShots=$Shots", "-ScctShotEvery=$Every", '-ScctShotUI', "-ScctShotDir=`"$shotDir`"",
        "-abslog=`"$log`"",
        $Extra
    ) -join ' '
    $run = Start-Process -FilePath "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $arguments `
        -NoNewWindow -PassThru `
        -RedirectStandardOutput (Join-Path $logs "online-$Tag.stdout.txt") `
        -RedirectStandardError (Join-Path $logs "online-$Tag.stderr.txt")
    if ($server -and $KillServerAfter -gt 0 -and -not $run.WaitForExit([int]($KillServerAfter * 1000))) {
        $server.Kill($true)
        "server stopped under the game after $KillServerAfter s"
    }
    $run.WaitForExit()
    "game exit code $($run.ExitCode)"
}
finally {
    if ($server -and -not $server.HasExited) {
        # Give the server a moment to log the player's leave, then stop it and what it started (dotnet run's child).
        Start-Sleep -Seconds 2
        $server.Kill($true)
        $server.WaitForExit(5000) | Out-Null
        'server stopped'
    }
    if ($work) { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }
}

Select-String -Path $log -Pattern 'LogOpenSCCT: (Online|Session|Level|Menu|-Scct)|LogOpenSCCT: (Error|Warning)' |
    ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[[^\]]+\]', '' } | Select-Object -First 120
