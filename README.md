# OpenSCCT Unreal

OpenSCCT on Unreal Engine 5.8: the same game, servers and netcode as
[OpenSCCT](https://github.com/Lumbridge/OpenSCCT), with a new Unreal presentation built for looks
rather than 1:1 parity with the original.

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

## Content rule

Original game content (maps, textures, meshes, sounds, animations, text) is never committed here, as in
OpenSCCT. It is imported locally from the player's own converted content.
