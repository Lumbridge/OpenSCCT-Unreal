# OpenSCCT Unreal

OpenSCCT on Unreal Engine 5.8: the same game, servers and netcode as
[OpenSCCT](https://github.com/OpenSCCT/OpenSCCT), with a new Unreal presentation built for looks
rather than 1:1 parity with the original.

The [OpenSCCT project directory](https://github.com/OpenSCCT/OpenSCCT) covers the original
PS2 disc edition, the original PC/Steam edition and this modern Unreal client.

For a custom Versus map, build the project with [OpenSCCT Editor](https://github.com/OpenSCCT/OpenSCCT-Editor)
and choose **Play in Unreal**. To run a compiled map from outside the editor, start the client with
`"--svm-maps=<folder containing your map folders>"`. The client reads the map's SVMap files and
`render/world.glb`. For distribution through the cooked content catalog, see the [map pak guide](docs/MAP-PAKS.md).

## How it fits together

- **Gameplay and netcode** stay in OpenSCCT's C# simulation. The Unreal client hosts those libraries
  in-process and connects to the existing OpenSCCT servers, so Unreal and Godot players share matches.
- **Unreal** owns everything you see and hear: Lumen lighting, Nanite geometry, virtual shadow maps,
  materials, characters, weapons, effects, audio, HUD and menus.
- **Maps**: the first map pool is the Enhanced maps. They are imported from a converted OpenSCCT
  content folder and then relit and re-surfaced in Unreal.

## Layout

| Path | What |
|---|---|
| `Source/OpenSCCT` | Unreal game module (C++) |
| `Bridge/` | C# bridge assembly that exposes the OpenSCCT client simulation to Unreal |
| `Content/` | Unreal assets made for this project |
| `Content/Imported/` | Imported original game content. Never committed. |
| `docs/` | Design notes |

## Building

Needs Unreal Engine 5.8.4, Visual Studio with the C++ game workload and the .NET 10 SDK.

```powershell
& "F:\UE_5.8\Engine\Build\BatchFiles\Build.bat" OpenSCCTEditor Win64 Development -project="$PWD\OpenSCCTUnreal.uproject" -waitmutex
```

Build the bridge too, before the first run and after any C# change (output lands in `Binaries/Bridge`):

```powershell
dotnet build -c Release Bridge\OpenScct.UnrealBridge -p:OpenScctRoot=<OpenSCCT checkout>\
```

## Assets after a fresh checkout

Build the C++ first (the weapon table and the front end level name classes of the game module). Then, in this
order, with `$c` the converted content folder. Every script runs headless (no window), prints its own log lines
and ends with exit code 0 only when its editor script really finished:

```powershell
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

## Playing a level

An imported level starts a local match with bots when it opens. Game URL options (all optional, see
`Source/OpenSCCT/Play/ScctGameMode.h`): `?Side=spy|merc`, `?Bots=3`, `?Mode=Story`,
`?Difficulty=easy|normal|hard|expert`, `?Sidearms=1`, `?Rifles=1`, `?Practice=1` (no time limit, unlimited
lives, unlimited gear and the practice aids such as `scct.GrabArea`), `?Name=Sam`, `?ScctMap=AquaE`. The converted content folder comes from `-ScctContent=<folder>`, else
the `SVM_CONTENT` variable, else `ContentRoot` under `[OpenSCCT]` in the game ini.

The TestRange drills (`scct.Drill vision-cancelling|spy-grab`, or TestRange with `?Drill=<kind>`) need the fixture
level: `Tools\Test\Import-Fixtures.ps1 -Content <folder>` imports it.

`Tools\Run-Offscreen.ps1 -Map AquaE -Content <folder> -Shots 3` plays a level with no window, saves
screenshots to `Saved\ScctShots` and quits.

## Front end and online play

The game starts in the front end (`/Game/OpenSCCT/Maps/L_FrontEnd`, built by `Tools\Menu\Build-FrontEnd.ps1`;
the menu is C++ in `Source/OpenSCCT/Menu`): play offline, join a server by address, or pick one from the master
server's list. The servers are OpenSCCT's own: the bridge speaks their protocol, so the Unreal game joins the
same dedicated and listen servers as the Godot client (`Source/OpenSCCT/Online/ScctOnlineSubsystem.h` describes
the flow: lobby in the front end, each round in the imported level of its map). The master server and invite
key are under `[OpenSCCT.Online]` in `Config/DefaultGame.ini`.

`Tools\Run-OnlineCheck.ps1 -Content <folder> -OpenScct <OpenSCCT checkout>` starts a real dedicated server on
localhost with bots, joins it with the game off-screen, plays a round and saves screenshots and logs.
`Tools\Run-HostCheck.ps1 -Map AquaE -Content <folder>` does the same for hosting, with two or more game processes
and no OpenSCCT checkout: one hosts, the others join it, and each lobby and round step (password, side, settings
un-readying, team chat, kick, rejoin by address, bots filling 2v2, a revenge pair and results Next) prints PASS or
FAIL from both sides' logs.

## Packaging

The bridge is staged with the game as loose files (`RuntimeDependencies` in `OpenSCCT.Build.cs`), so build
it before packaging. The .NET runtime is looked for in this order: `Binaries/DotNet` (a private runtime laid
out as a .NET install root: copy the contents of a .NET 10 runtime zip there and players need nothing
installed), the `DOTNET_ROOT` variable, `Program Files\dotnet`. The converted content folder is not staged:
it is the player's own.

`Tools\Package-Release.ps1` makes a release: a Shipping build of the `OpenSCCTUnreal` target, with the bridge and
a private .NET runtime, zipped with its checksums and the launcher handshake. Nothing under `/Game/Imported` is
cooked. `docs/RELEASING.md` has the procedure.

## Content rule

Original game content (maps, textures, meshes, sounds, animations, text) is never committed here, as in
OpenSCCT. It is imported locally from the player's own converted content.
