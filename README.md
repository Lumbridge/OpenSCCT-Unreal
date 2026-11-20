# OpenSCCT Unreal

OpenSCCT Unreal is a standalone Unreal Engine 5.8 edition of Spies vs. Mercenaries. This repository
contains the game module, its C# simulation and server, and the tools that convert a player's Steam installation.

The [OpenSCCT project directory](https://github.com/OpenSCCT/OpenSCCT) covers the original
PS2 disc edition, the original PC/Steam edition and this modern Unreal client.

For a custom Versus map, build the project with [OpenSCCT Editor](https://github.com/OpenSCCT/OpenSCCT-Editor)
and choose **Play in Unreal**. To run a compiled map from outside the editor, start the client with
`"--svm-maps=<folder containing your map folders>"`. The client reads the map's SVMap files and
`render/world.glb`. For distribution through the cooked content catalog, see the [map pak guide](docs/MAP-PAKS.md).

## How it fits together

- **Gameplay and netcode** live in the C# simulation under `Runtime/`. Unreal hosts the simulation
  in-process and connects to dedicated and listen servers running a compatible protocol.
- **Unreal** owns everything you see and hear: Lumen lighting, Nanite geometry, virtual shadow maps,
  materials, characters, weapons, effects, audio, HUD and menus.
- **Maps**: the first map pool is the Enhanced maps. They are imported from a converted OpenSCCT
  content folder and then relit and re-surfaced in Unreal.

## Layout

| Path | What |
|---|---|
| `Source/OpenSCCT` | Unreal game module (C++) |
| `Bridge/` | C# bridge assembly that exposes the OpenSCCT client simulation to Unreal |
| `Runtime/` | Source for the simulation, networking, dedicated server and Steam converter used by this edition |
| `Tools/SteamSetup/` and `Tools/UnrealContent/` | Windows Steam setup and the release converter recipe |
| `Content/` | Unreal assets made for this project |
| `Content/Imported/` | Imported original game content. Never committed. |
| `docs/` | Design notes |

## Building

Clone this repository with Git LFS installed. The Windows editor build needs Unreal Engine 5.8.4,
Visual Studio with the C++ game workload, and the .NET 10 SDK specified in `Runtime/global.json`.

```powershell
& "F:\UE_5.8\Engine\Build\BatchFiles\Build.bat" OpenSCCTEditor Win64 Development -project="$PWD\OpenSCCTUnreal.uproject" -waitmutex
```

Build the bridge before the first run and after C# gameplay changes. Its output goes to `Binaries/Bridge`.

```powershell
dotnet build -c Release Bridge\OpenScct.UnrealBridge
```

## Assets after a fresh checkout

Build the C++ module first. For the original assets, run this edition's Steam setup on an installed copy of
Splinter Cell Chaos Theory (Steam app 13570). On a development machine with Python 3.11, you can assemble
and launch the same setup from the checkout. Select the Steam game folder when prompted.

```powershell
py -3.11 Tools/UnrealContent/package.py Saved/UnrealDev/contenttools
dotnet Saved/UnrealDev/contenttools/setup/OpenScct.UnrealSetup.dll
```

Setup records the active conversion in `steam-current.json`. The following commands select that build and import it into the editor. Set
`OPENSCCT_UNREAL_CONTENT_DIR` if setup used a custom installation directory.

```powershell
$install = if ($env:OPENSCCT_UNREAL_CONTENT_DIR) { $env:OPENSCCT_UNREAL_CONTENT_DIR } else { "$env:LOCALAPPDATA\OpenSCCT\unreal\content" }
$id = (Get-Content -Raw (Join-Path $install 'steam-current.json') | ConvertFrom-Json).build
$c = (Resolve-Path -LiteralPath (Join-Path $install "steam-builds\$id")).Path
$env:SVM_CONTENT = $c
Tools\Import\Import-Characters.ps1 -Content $c     # bodies, weapons, view models, goggle lenses -> Content\Imported\Chars
Tools\Effects\Build-EffectAssets.ps1 -Content $c   # the game's effect meshes -> Content\Imported\Fx
Tools\Import\Import-Map.ps1 -Map AquaE -Content $c # once per map -> Content\Imported\Maps\<Map>
```

Everything else is committed and only needs its script after a change to that script: the surface, glass, water
and sky materials (`Tools\Import\Import-Assets.ps1 -MaterialsOnly`; without the switch it also downloads the CC0
sets again), the map effect materials (`Tools\Import\Import-Effects.ps1`), the merc rifle
(`Tools\Import\Import-MercRifle.ps1`), the marker materials (`Tools\Level\Build-LevelAssets.ps1`), the goggles
(`Tools\Materials\Build-VisionMaterials.ps1`) and the front end level (`Tools\Menu\Build-FrontEnd.ps1`). They
come before the map imports when they are run. A rebuilt asset is a new binary file even when nothing changed.
The weapon table `DA_ScctWeapons` is made once: delete it and run `Import-Characters.ps1` to get the script's
values back.

Enhanced Mount Hospital's restored colour textures can be regenerated from the installed map.
[Texture restoration batch](docs/HOSPITAL-TEXTURE-RESTORATION.md)

## Playing a level

An imported level starts a local match with bots when it opens. Game URL options (all optional, see
`Source/OpenSCCT/Play/ScctGameMode.h`): `?Side=spy|merc`, `?Bots=3`, `?Mode=Story`,
`?Difficulty=easy|normal|hard|expert`, `?Sidearms=1`, `?Rifles=1`, `?Practice=1` (no time limit, unlimited
lives, unlimited gear and the practice aids such as `scct.GrabArea`), `?Name=Sam`, `?ScctMap=AquaE`.
For an editor run, `-ScctContent=<folder>` selects a converted content directory explicitly.
The packaged game uses the active content build prepared by its Steam setup.

The TestRange drills (`scct.Drill vision-cancelling|spy-grab`, or TestRange with `?Drill=<kind>`) need the fixture
level: `Tools\Test\Import-Fixtures.ps1 -Content <folder>` imports it.

`Tools\Run-Offscreen.ps1 -Map AquaE -Content <folder> -Shots 3` plays a level with no window, saves
screenshots to `Saved\ScctShots` and quits.

## Front end and online play

The front end offers offline play, a server browser and direct connections. It opens the lobby before
travelling to the imported level for each round. The master server and invite key are configured under
`[OpenSCCT.Online]` in `Config/DefaultGame.ini`. [Online subsystem](Source/OpenSCCT/Online/ScctOnlineSubsystem.h)

Build the dedicated server with `dotnet build -c Release Runtime/src/OpenScct.Server.Cli`. Then run
`Tools\Run-OnlineCheck.ps1 -Content <folder>` to start the server on localhost, join with the off-screen client
and save screenshots and logs from a round with bots. `Tools\Run-HostCheck.ps1 -Map AquaE -Content <folder>`
starts two or more local game processes. One hosts, the others join, and each lobby and round step (password, side, settings
un-readying, team chat, kick, rejoin by address, bots filling 2v2, a revenge pair and results Next) prints PASS or
FAIL from both sides' logs.

## Packaging

Unreal stages the bridge as loose files using `RuntimeDependencies` in `OpenSCCT.Build.cs`.
The shipping game loads its private .NET runtime from `Binaries/DotNet`. Development builds can
also locate .NET through `DOTNET_ROOT` or `Program Files\dotnet`.

`Tools\Package-Release.ps1` assembles the Windows Shipping build, the bridge, the private .NET runtime and
this edition's Steam conversion tools. The release guide covers the distribution manifest and content checks.
[Release procedure](docs/RELEASING.md)

## Content rule

Original game content (maps, textures, meshes, sounds, animations, text) is never committed here, as in
OpenSCCT. It is imported locally from the player's own converted content.
