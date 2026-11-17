# OpenSCCT Unreal: design

Started 2026-11-17.

## What Ryan asked for

- Port OpenSCCT to Unreal Engine 5.8.4, in its own private repo.
- 1:1 parity with the original is not the goal. It should look AAA and better than the Godot client:
  flashy, beautiful graphics, new textures and models, a cool gun for the merc.
- The first map pool is the Enhanced maps (the E maps: AquaE, CineE, ClubE, FactE, KrauE, MissE, MounE,
  MuseE, OrphE, PolaE, RiveE, SancE, ScheE, StatE, SteeE, VertE).
- Use the existing OpenSCCT server if possible; otherwise write a new server in Rust.
- Models and animations may be downloaded from online sources or made with local tools.

## Approaches considered

1. **Host the OpenSCCT C# client simulation inside Unreal (chosen).** Unreal loads the .NET runtime and a
   small bridge assembly that runs OpenSCCT's client session: networking, prediction, world state, animation
   channels and events. Unreal reads that state every frame and draws it. Works with the existing servers,
   the master server, bots and maps unchanged, and every gameplay fix lands in both clients. Cost: gameplay
   is not driven by Unreal's own character movement, replication or ability systems.
2. **Rewrite gameplay in Unreal C++ with Unreal replication.** The most "native" result, but it re-creates
   about 1,300 files of movement, rules, gear and netcode before anything is playable, cannot join the
   existing servers, and splits every later gameplay change in two.
3. **New Rust server plus native Unreal client.** Same rewrite cost as 2 on the client and a second rewrite
   on the server. Only worth it if 1 turned out impossible.

Ryan's preference for the existing server picks 1. The Rust fallback is not needed unless hosting .NET in
Unreal fails.

## Calls made (mine, open to correction)

- **Repo**: `Lumbridge/OpenSCCT-Unreal`, private. Project `OpenSCCTUnreal`, game module `OpenSCCT`.
- **Runtime hosting**: the full .NET runtime through `hostfxr`, not NativeAOT, so the simulation runs
  unmodified whatever it does with reflection. The bridge exposes a flat C interface.
- **OpenSCCT source**: referenced from a sibling checkout of `Lumbridge/OpenSCCT` at a pinned commit, not
  copied in.
- **Content**: original game content is never committed, as in OpenSCCT. Maps are imported locally from a
  converted `.content` folder into `Content/Imported/` (ignored by Git). New art made for this project is
  committed through Git LFS, with source and licence recorded in `docs/ASSETS.md`.
- **Rendering**: DirectX 12, Lumen with hardware ray tracing, Nanite, virtual shadow maps, Substrate
  materials, fully dynamic lighting (no baked lightmaps). Target hardware: RTX 4070 class.
- **Look**: imported levels keep their layout and collision (the simulation owns collision) and get new
  PBR materials, relit light sources, volumetric fog, reflections and post-processing. Characters and
  weapons are new models driven by the simulation's animation channels.

## Milestones

1. Project compiles, repo exists.
2. Bridge: Unreal starts .NET, the bridge runs a client session and reports state.
3. One Enhanced map imported and lit in Unreal.
4. Connect to an OpenSCCT server, see pawns move, drive the local player.
5. Characters, animation, first person view, the merc's gun.
6. Effects, audio, HUD, menus.
7. The remaining Enhanced maps, look pass per map.
