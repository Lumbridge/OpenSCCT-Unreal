<#
.SYNOPSIS
Proves hosting end to end with two game processes on this machine: one hosts, the other joins, nobody at the keyboard.

.DESCRIPTION
The Unreal counterpart of the Godot client's net-lobby and net-bots smokes (game/Scripts/Online/NetLobbySmoke.cs,
NetBotsSmoke.cs). Every process is the game itself started off-screen; the host is a listen server in its process
(-ScctHost, loopback only, unlisted), the others join it over UDP on 127.0.0.1. The steps are driven by the
unattended switches of Online/ScctOnlineSubsystem.h, Menu/SScctMenuLobby.cpp, Menu/SScctMatchMenu.h and the menu
script of Menu/ScctMenuDriver.h, and each step is judged from the logs of both sides.

Lobby (port -Port):
  host up            HostA hosts -Map with a password, waiting for more people than will come (no launch)
  wrong password     a guest with the wrong password is refused with the server's reason
  password and side  GuestB joins with the password on the mercenaries and says ready
  settings change    HostA changes Respawn Length in Session Options (AquaE Story has 10) and applies: GuestB is un-readied
  team chat          HostA's team line reaches the spies only (GuestB never sees it); GuestB's line reaches HostA
  kick               HostA kicks GuestB, whose game says so
  rejoin by address  GuestB comes back through the menus: Online, Join by Address, the address, the password, CONNECT
Round (port -Port + 2):
  bots fill 2v2      HostA hosts 4 slots with bots filling the empty places; GuestB joins as a merc and a merc bot
                     makes room: two spies and two mercs, one bot on each side
  launch             HostA launches once GuestB is ready (bots are not counted as people)
  round played       both play the round on -Map with four pawns, and its results come in
  revenge pair       a revenge pair: two games, sides swapped for the second
  results Next       both press Next on each results screen and are back in the lobby after the second game

Voice (Godot's 440 Hz tone and the volume slider) and hubs are not here: they belong to other parts of the client.

Logs go to Saved\Logs\hostcheck-<Tag>-<process>.log, screenshots (the interface included) to
Saved\ScctShots\<Tag>\<process>\. Each step prints PASS or FAIL; the exit code is 0 only when every step passed. Only
the processes this script started are stopped, and only once their steps are judged.

.EXAMPLE
Tools\Run-HostCheck.ps1 -Map AquaE -Content C:\path\to\.content

.EXAMPLE
# Only the round, with 2-minute games.
Tools\Run-HostCheck.ps1 -Map AquaE -Content C:\path\to\.content -Only round -TimeLimit 2
#>
param(
    [string] $Content = $env:SVM_CONTENT,
    [string] $Map = 'AquaE',
    # The lobby's port; the round's server uses -Port + 2.
    [int] $Port = 28971,
    [string] $Password = 'hostcheck',
    # Minutes a game of the revenge pair lasts.
    [int] $TimeLimit = 1,
    # Seconds between screenshots.
    [double] $Every = 6,
    [ValidateSet('all', 'lobby', 'round')] [string] $Only = 'all',
    [string] $Tag = 'hostcheck',
    # Seconds a game may take to come up: a game started while another project's Build.bat runs waits for it (the
    # editor's platform check goes through Build.bat), so this is generous.
    [int] $StartWait = 900,
    [int] $ResX = 1280,
    [int] $ResY = 720,
    [string] $Engine = 'F:\UE_5.8'
)

$ErrorActionPreference = 'Stop'
if (-not $Content) { throw 'Pass -Content <converted content folder> (or set SVM_CONTENT).' }
$project = (Resolve-Path "$PSScriptRoot\..").Path
$logs = Join-Path $project 'Saved\Logs'
New-Item -ItemType Directory -Force $logs | Out-Null
$editor = "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$contentDir = "$Content".Replace('\', '/')

$script:Results = [System.Collections.Generic.List[object]]::new()
$script:Games = [System.Collections.Generic.List[object]]::new()

function Format-HostCheckSwitch([string] $Key, [string] $Value) {
    # Unreal reads -Key="a value" whole; a quote inside the value would end it early.
    if ($Value.Contains('"')) { throw "The value of -$Key must not contain a double quote: $Value" }
    "-$Key=`"$Value`""
}

# Starts one game process off-screen at the front end; Switches are its own unattended switches, as written.
# A game quits once its screenshots are taken, so a host is given a long interval (ShotEvery) to outlast guests that are
# slow to start.
function Start-HostCheckGame([string] $Name, [string[]] $Switches, [int] $Shots, [double] $ShotEvery = $Every) {
    $log = (Join-Path $logs "hostcheck-$Tag-$Name.log").Replace('\', '/')
    $shotDir = (Join-Path $project "Saved\ScctShots\$Tag\$Name").Replace('\', '/')
    if (Test-Path $log) { Remove-Item $log -Force }
    $arguments = @(
        "`"$project\OpenSCCTUnreal.uproject`"",
        '-game', '-RenderOffscreen', "-ResX=$ResX", "-ResY=$ResY", '-ForceRes', '-unattended', '-nosplash', '-nosound',
        (Format-HostCheckSwitch 'ScctContent' $contentDir),
        "-ScctShots=$Shots", "-ScctShotEvery=$ShotEvery", '-ScctShotUI', (Format-HostCheckSwitch 'ScctShotDir' $shotDir),
        (Format-HostCheckSwitch 'abslog' $log)
    ) + $Switches
    $process = Start-Process -FilePath $editor -ArgumentList ($arguments -join ' ') -NoNewWindow -PassThru `
        -RedirectStandardOutput (Join-Path $logs "hostcheck-$Tag-$Name.stdout.txt") `
        -RedirectStandardError (Join-Path $logs "hostcheck-$Tag-$Name.stderr.txt")
    $game = [pscustomobject]@{ Name = $Name; Process = $process; Log = $log; Shots = $shotDir }
    $script:Games.Add($game)
    Write-Host "  started $Name (process $($process.Id))"
    return $game
}

# The game's own lines of a log (LogOpenSCCT), read while the game still writes it.
function Get-HostCheckLines($Game) {
    if (-not (Test-Path $Game.Log)) { return @() }
    $stream = [IO.File]::Open($Game.Log, 'Open', 'Read', 'ReadWrite, Delete')
    try {
        $reader = [IO.StreamReader]::new($stream)
        $text = $reader.ReadToEnd()
    } finally { $stream.Dispose() }
    return @($text -split "`r?`n" | Where-Object { $_ -like '*LogOpenSCCT:*' } | ForEach-Object { $_ -replace '^.*?LogOpenSCCT: ', '' })
}

# Waits until Test (a script block given the game's lines) returns something, the game ends or the time is up.
function Wait-HostCheck($Game, [int] $Seconds, [scriptblock] $Test) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ($true) {
        $lines = Get-HostCheckLines $Game
        $found = & $Test $lines
        if ($found) { return $found }
        if ($Game.Process.HasExited -or (Get-Date) -gt $deadline) {
            # One more look: the last lines may have been written as the game closed.
            return (& $Test (Get-HostCheckLines $Game))
        }
        Start-Sleep -Milliseconds 500
    }
}

# The first line matching Pattern (a regular expression), from line After on, or $null.
function Find-HostCheckLine([string[]] $Lines, [string] $Pattern, [int] $After = -1) {
    for ($i = [Math]::Max(0, $After + 1); $i -lt $Lines.Count; $i++) {
        if ($Lines[$i] -match $Pattern) { return [pscustomobject]@{ Index = $i; Line = $Lines[$i] } }
    }
    return $null
}

function Wait-HostCheckLine($Game, [string] $Pattern, [int] $Seconds, [int] $After = -1) {
    Wait-HostCheck $Game $Seconds { param($lines) Find-HostCheckLine $lines $Pattern $After }
}

function Write-HostCheckStep([string] $Step, [bool] $Passed, [string] $Evidence) {
    $script:Results.Add([pscustomobject]@{ Step = $Step; Passed = $Passed; Evidence = $Evidence })
    $word = if ($Passed) { 'PASS' } else { 'FAIL' }
    $colour = if ($Passed) { 'Green' } else { 'Red' }
    Write-Host ("{0}  {1,-18} {2}" -f $word, $Step, $Evidence) -ForegroundColor $colour
}

function Short([object] $Found) { if ($Found) { $Found.Line.Substring(0, [Math]::Min(150, $Found.Line.Length)) } else { 'not seen' } }

# The lobby's players from a roster line ("Online: lobby players: A (spy, ready, map ok), B (merc, ready, map ok, bot)").
function Read-HostCheckRoster([string] $Line) {
    [regex]::Matches($Line, '(?:: |, )([^,()]+?) \((spy|merc|watching), (ready|not ready), map [^,)]+(, bot)?\)') |
        ForEach-Object { [pscustomobject]@{ Name = $_.Groups[1].Value; Side = $_.Groups[2].Value; Ready = $_.Groups[3].Value -eq 'ready'; Bot = $_.Groups[4].Success } }
}

function Stop-HostCheckGame($Game, [int] $GraceSeconds = 0) {
    if ($GraceSeconds -gt 0) { $Game.Process.WaitForExit($GraceSeconds * 1000) | Out-Null }
    if (-not $Game.Process.HasExited) {
        $Game.Process.Kill($true)
        $Game.Process.WaitForExit(10000) | Out-Null
        "  stopped $($Game.Name)"
    } else {
        "  $($Game.Name) ended by itself (exit code $($Game.Process.ExitCode))"
    }
}

function Invoke-HostCheckLobby {
    "Lobby on 127.0.0.1:$Port, map $Map"
    $teamLine = 'Spies only: hold the vault'
    $guestLine = 'Hello from the guest'
    $hostGame = Start-HostCheckGame 'lobby-host' @(
        "-ScctHost=$Port", "-ScctHostMap=$Map", (Format-HostCheckSwitch 'ScctHostName' 'Host check'),
        "-ScctHostPassword=$Password", '-ScctLaunchWhen=9', '-ScctSide=spy', '-ScctName=HostA', '-ScctRunIdentity',
        (Format-HostCheckSwitch 'ScctLobbyApply' '?TimesRespawnsATT=15?Tab=rules'), '-ScctLobbyApplyWhen=2', '-ScctLobbyApplyAfter=8',
        (Format-HostCheckSwitch 'ScctChatTeam' $teamLine), '-ScctChatWhen=2', '-ScctKickAfter=GuestB,30') 40 ([Math]::Max($Every, (2 * $StartWait + 300) / 40))

    $up = Wait-HostCheckLine $hostGame "the host listens on UDP port $Port" $StartWait
    Write-HostCheckStep 'host up' ([bool] $up) (Short $up)
    if (-not $up) { return }

    # The refused guest is a new install of its own; GuestB is this folder's saved install, so the GuestB that comes
    # back after the kick is the same player.
    $wrong = Start-HostCheckGame 'lobby-wrongpw' @("-ScctJoin=127.0.0.1:$Port", '-ScctJoinPassword=not-it', '-ScctName=GuestX', '-ScctRunIdentity') 4
    $guest = Start-HostCheckGame 'lobby-guest' @(
        "-ScctJoin=127.0.0.1:$Port", "-ScctJoinPassword=$Password", '-ScctSide=merc', '-ScctName=GuestB', '-ScctAutoReady',
        (Format-HostCheckSwitch 'ScctChat' $guestLine)) 30

    # Wrong password: the server's reason, and HostA never lists the player.
    $refused = Wait-HostCheckLine $wrong 'the visit failed.*Wrong password' $StartWait
    $listed = Find-HostCheckLine (Get-HostCheckLines $hostGame) 'lobby players:.*GuestX'
    Write-HostCheckStep 'wrong password' ([bool] $refused -and -not $listed) $(if ($listed) { "HostA listed GuestX: $(Short $listed)" } else { Short $refused })

    # Password and side: GuestB reaches the lobby on the mercenaries, and both sides list GuestB there.
    $inLobby = Wait-HostCheckLine $guest 'state Connecting -> Lobby' $StartWait
    $hostSees = Wait-HostCheckLine $hostGame 'lobby players:.*GuestB \(merc' 30
    $guestSees = Wait-HostCheckLine $guest 'lobby players:.*HostA \(spy.*GuestB \(merc|lobby players:.*GuestB \(merc.*HostA \(spy' 30
    Write-HostCheckStep 'password and side' ([bool] ($inLobby -and $hostSees -and $guestSees)) "guest: $(Short $inLobby) | host: $(Short $hostSees)"

    # Settings change un-readies: HostA applies Respawn Length 15; GuestB was ready, the new setup reaches GuestB and
    # the server un-readies GuestB (GuestB's -ScctAutoReady then says ready again).
    $applied = Wait-HostCheckLine $hostGame 'asked the server to change the setup:.*TimesRespawnsATT=15' 60
    $unready = Wait-HostCheck $guest 30 {
        param($lines)
        $ready = Find-HostCheckLine $lines 'lobby players:.*GuestB \(merc, ready'
        $setup = Find-HostCheckLine $lines "the lobby's setup is now .*TimesRespawnsATT=15"
        if (-not $ready -or -not $setup) { return $null }
        if ($ready.Index -gt $setup.Index) { return $null }
        return Find-HostCheckLine $lines 'lobby players:.*GuestB \(merc, not ready' $setup.Index
    }
    # HostA's side: its server took the change and announced it. HostA's own roster is not asked to show GuestB not
    # ready: GuestB says ready again at once, and a host loaded by several games on one machine runs at a few frames a
    # second, so the not-ready moment can fall between two of its roster reads.
    $announced = $null
    if ($applied) { $announced = Wait-HostCheckLine $hostGame 'Online host: HostA set up .*respawn 15 s' 30 $applied.Index }
    Write-HostCheckStep 'settings change' ([bool] ($applied -and $announced -and $unready)) "host: $(Short $announced) | guest: $(Short $unready)"

    # Team chat: HostA's line to the spies comes back to HostA marked as a team line, GuestB's line to everyone
    # reaches HostA, and GuestB (a merc) never receives HostA's.
    $teamSent = Wait-HostCheckLine $hostGame "\[team only, spies\] <HostA> $([regex]::Escape($teamLine))" 60
    $allHeard = Wait-HostCheckLine $hostGame "Online chat: <GuestB> $([regex]::Escape($guestLine))" 30

    # Kick: HostA kicks GuestB, and GuestB's game says it was kicked. The team line had to be judged after it was
    # sent and while GuestB was still in the lobby to hear it, so GuestB's log is read once the kick is in.
    $kickSent = Wait-HostCheckLine $hostGame "asked the server to kick 'GuestB'" 90
    $kicked = Wait-HostCheckLine $guest 'the visit failed \(status Kicked.*Kicked by' 30
    $leak = Find-HostCheckLine (Get-HostCheckLines $guest) ([regex]::Escape($teamLine))
    Write-HostCheckStep 'team chat' ([bool] ($teamSent -and $allHeard) -and -not $leak) $(if ($leak) { "GuestB received the spies' line: $(Short $leak)" } else { "host: $(Short $teamSent) | $(Short $allHeard)" })
    Write-HostCheckStep 'kick' ([bool] ($kickSent -and $kicked)) "host: $(Short $kickSent) | guest: $(Short $kicked)"
    Stop-HostCheckGame $guest 3
    Stop-HostCheckGame $wrong

    # Rejoin by address, through the menus as a player does it: the address field is emptied first (it holds the
    # last address joined), Tab moves to the password, CONNECT joins on the smaller side (the mercenaries).
    $menu = "wait 4; page online/address; wait 2; where; key End; key BackSpace 40; type 127.0.0.1:$Port; key Tab; type $Password; wait 1; shot rejoin_form; press CONNECT; wait 10; shot rejoin_lobby; wait 2; quit"
    $back = Start-HostCheckGame 'lobby-rejoin' @((Format-HostCheckSwitch 'ScctMenuDo' $menu), '-ScctName=GuestB') 30
    $kickIndex = if ($kickSent) { $kickSent.Index } else { -1 }
    $joining = Wait-HostCheckLine $back "Menu: joining 127.0.0.1:$Port" $StartWait
    $backIn = Wait-HostCheckLine $back 'state Connecting -> Lobby' 60
    $hostSeesBack = Wait-HostCheckLine $hostGame 'lobby players:.*GuestB \(merc' 30 $kickIndex
    Write-HostCheckStep 'rejoin by address' ([bool] ($joining -and $backIn -and $hostSeesBack)) "guest: $(Short $joining) | host: $(Short $hostSeesBack)"
    Stop-HostCheckGame $back 30
    Stop-HostCheckGame $hostGame
}

function Invoke-HostCheckRound {
    $roundPort = $Port + 2
    "Round on 127.0.0.1:$roundPort, map $Map, a revenge pair of $TimeLimit-minute games, bots filling 4 slots"
    $hostGame = Start-HostCheckGame 'round-host' @(
        "-ScctHost=$roundPort", "-ScctHostMap=$Map", (Format-HostCheckSwitch 'ScctHostName' 'Bots check'), '-ScctHostPlayers=4',
        (Format-HostCheckSwitch 'ScctHostOptions' "?TimeLimit=${TimeLimit}?Revenge=true?SvmBots=2?SvmBotSkill=0"),  # braces: a ? may be part of a variable's name
        '-ScctLaunchWhen=2', '-ScctSide=spy', '-ScctName=HostA', '-ScctRunIdentity', '-ScctResultsAction=next') 80 ([Math]::Max($Every, ($StartWait + 120) / 80))
    $up = Wait-HostCheckLine $hostGame "the host listens on UDP port $roundPort" $StartWait
    if (-not $up) {
        Write-HostCheckStep 'bots fill 2v2' $false 'the round host did not come up'
        Stop-HostCheckGame $hostGame
        return
    }
    $guest = Start-HostCheckGame 'round-guest' @(
        "-ScctJoin=127.0.0.1:$roundPort", '-ScctSide=merc', '-ScctName=GuestB', '-ScctAutoReady', '-ScctAutoWalk', '-ScctResultsAction=next') 80

    # Bots fill 2v2: the host's roster once GuestB is in has two spies and two mercs, one bot on each side.
    $roster = Wait-HostCheck $hostGame $StartWait {
        param($lines)
        for ($i = $lines.Count - 1; $i -ge 0; $i--) {
            if ($lines[$i] -match 'lobby players:.*GuestB \(merc') {
                $players = @(Read-HostCheckRoster $lines[$i])
                $spies = @($players | Where-Object Side -eq 'spy')
                $mercs = @($players | Where-Object Side -eq 'merc')
                if ($spies.Count -eq 2 -and $mercs.Count -eq 2 -and @($spies | Where-Object Bot).Count -eq 1 -and @($mercs | Where-Object Bot).Count -eq 1) {
                    return [pscustomobject]@{ Index = $i; Line = $lines[$i] }
                }
            }
        }
        return $null
    }
    $guestRoster = Find-HostCheckLine (Get-HostCheckLines $guest) 'lobby players:.*, bot\)'
    Write-HostCheckStep 'bots fill 2v2' ([bool] ($roster -and $guestRoster)) (Short $roster)

    # Launch: once GuestB is ready, with four players of whom two are people.
    $launch = Wait-HostCheckLine $hostGame '-ScctLaunchWhen: launching .* with 4 player\(s\), 2 of them people' 90
    Write-HostCheckStep 'launch' ([bool] $launch) (Short $launch)

    # Round played: both load the map, see four pawns and get the results.
    $wait = 120 + 2 * 60 * $TimeLimit
    $roundOk = $true
    $evidence = @()
    foreach ($game in @($hostGame, $guest)) {
        $plays = Wait-HostCheckLine $game "Session \d+ plays: map $Map" $wait
        $pawns = Wait-HostCheckLine $game "remote session \d+ on $Map, \d+ client ticks, 4 pawns" 60
        $results = Wait-HostCheckLine $game 'results are in:' $wait
        $roundOk = $roundOk -and [bool] ($plays -and $pawns -and $results)
        $evidence += "$($game.Name): $(if ($pawns) { '4 pawns' } else { 'no 4-pawn line' }), $(Short $results)"
    }
    Write-HostCheckStep 'round played' $roundOk ($evidence -join ' | ')

    # Revenge pair and results Next: game 1 leaves one game, Next on both, game 2 with the sides swapped, no game
    # left, Next on both, and both back in the lobby.
    $revengeOk = $true
    $nextOk = $true
    $revengeEvidence = @()
    $nextEvidence = @()
    foreach ($game in @($hostGame, $guest)) {
        $first = Wait-HostCheckLine $game 'results are in:.* 1 game\(s\) left' $wait
        $next1 = if ($first) { Wait-HostCheckLine $game 'Results: Next, ready for the next game of the series' 60 $first.Index } else { $null }
        $second = if ($first) { Wait-HostCheckLine $game 'results are in:.* 0 game\(s\) left' $wait $first.Index } else { $null }
        $next2 = if ($second) { Wait-HostCheckLine $game 'Results: Next, ready for the lobby' 60 $second.Index } else { $null }
        $lobby = if ($next2) { Wait-HostCheckLine $game 'state \w+ -> Lobby' 90 $next2.Index } else { $null }
        $lines = Get-HostCheckLines $game
        # The side each game was played on, from the local pawn of that session ("#683 merc team 1 LOCAL"): the
        # session's "plays" line names the side asked for in the lobby, which the revenge swap does not change.
        $sideBySession = [ordered]@{}
        foreach ($line in $lines) {
            if ($line -match "remote session (\d+) on $Map, \d+ client ticks, \d+ pawns:.*#\d+ (spy|merc) team \d+ LOCAL" -and -not $sideBySession.Contains($Matches[1])) {
                $sideBySession[$Matches[1]] = $Matches[2]
            }
        }
        $sides = @($sideBySession.Values)
        $swapped = $sides.Count -ge 2 -and $sides[0] -ne $sides[1]
        $revengeOk = $revengeOk -and [bool] ($first -and $second) -and $swapped
        $nextOk = $nextOk -and [bool] ($next1 -and $next2 -and $lobby)
        $revengeEvidence += "$($game.Name): games $(if ($second) { 2 } elseif ($first) { 1 } else { 0 }), sides $($sides -join ' then ')"
        $nextEvidence += "$($game.Name): $(if ($next1) { 'Next' } else { 'no Next' }), $(if ($next2) { 'Next' } else { 'no Next' }), $(Short $lobby)"
    }
    Write-HostCheckStep 'revenge pair' $revengeOk ($revengeEvidence -join ' | ')
    Write-HostCheckStep 'results Next' $nextOk ($nextEvidence -join ' | ')
    Stop-HostCheckGame $guest
    Stop-HostCheckGame $hostGame
}

try {
    if ($Only -in 'all', 'lobby') { Invoke-HostCheckLobby }
    if ($Only -in 'all', 'round') { Invoke-HostCheckRound }
}
finally {
    foreach ($game in $script:Games) {
        if (-not $game.Process.HasExited) { $game.Process.Kill($true) }
    }
}

''
foreach ($game in $script:Games) {
    $count = if (Test-Path $game.Shots) { @(Get-ChildItem $game.Shots -Filter *.png).Count } else { 0 }
    "  $($game.Name): $count screenshot(s) in $($game.Shots), log $($game.Log)"
}
$failed = @($script:Results | Where-Object { -not $_.Passed })
''
"$($script:Results.Count - $failed.Count) of $($script:Results.Count) steps passed"
if ($failed.Count -gt 0 -or $script:Results.Count -eq 0) { exit 1 }
exit 0
