# OpenSCCT Unreal: design

Started 2026-11-17.

## Goals

- Port OpenSCCT to Unreal Engine 5.8.4, in its own repository.
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

The preference for the existing server picks 1. The Rust fallback is not needed unless hosting .NET in
Unreal fails.

## Calls made (mine, open to correction)

- **Repo**: `Lumbridge/OpenSCCT-Unreal`, public. Its GitHub releases are what the OpenSCCT launcher installs.
  Project `OpenSCCTUnreal`, game module `OpenSCCT`.
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

## Menus

The menus are Slate, built in code like the HUD, and share its visual language (`Hud/ScctHudStyle.h`); the
widget set is `Menu/ScctMenuKit.h`, the pages are one file each under `Source/OpenSCCT/Menu`.

**Look.** A quiet tactical display over a living scene, not a panel of buttons:

- *Backdrop*: an imported map streamed in behind the menu (`AScctFrontEndGameMode`), seen through a slow camera
  that drifts and pushes in, with its own post-process (shallow depth of field, a vignette, a little grain and
  a cooler grade), and a left-to-right darkening in Slate so the type always sits on dark. Without an imported
  map the level's own small lit scene stays.
- *Type*: one family (the engine's Roboto) in a fixed scale: display 64 (the wordmark, a results headline), H1 30
  light with wide spacing (a page title), H2 19 (a panel title), body 15, small 13, and spaced bold capitals 11 to
  13 for labels, buttons and table heads. Nothing smaller than 11 units, and pages are laid out for 1600 x 900
  units and drawn up to a quarter larger at 720p (`ScctUi::ScaleForSmallScreens`), so 720p type stays readable.
- *Spacing*: an 8-unit grid; rows 44 high, panels padded 28, gaps 8/16/24/48.
- *Surfaces*: glass panels (a blur of the scene, a dark tint, a hairline edge, 8 corner); nothing is a flat grey box.
- *Colour*: white type at three strengths (text, dim, faint); one accent at a time: the spies' cyan in the menus,
  the merc amber wherever the merc side is meant, red only for leaving and errors, green only for "ready".
- *States*: one highlight for the pointer and the focus (pointing at a control focuses it), drawn as an accent
  edge and a brighter fill that ease in over about 60 ms; the focused primary button also gets an outer ring.
  Disabled controls fade to 40 %.
- *Motion*: a page slides up 16 units and fades in over 0.25 s; dialogs fade with a blur behind; toasts slide in
  from the right; nothing jumps (the page column is anchored to the top, so pages of different heights keep the
  title in the same place).
- *Sound*: the content's own interface sounds (`shared/audio/wav/Interface/Menu`: move, accept, back, open,
  notice) and the Entry level's menu song, each with its own volume.

**Input.** Everything works with the mouse, the keyboard and a controller. Arrows, D-pad and left stick move the
focus; Enter, Space and A press; Left/Right change a value; Escape and B go back (or close the dialog); Q/E,
Page Up/Down and the shoulders change tabs. The footer shows the keys that work on the page, as keyboard keys or
controller buttons depending on the last input.

**Pages and navigation.** The front end is a stack of pages over the backdrop (`Menu/SScctFrontEnd`); Back pops it.

```
Main menu ─┬─ Play offline (map browser, mode, side, bots, rules) ── START ─> match
           ├─ Online ─┬─ Server list (sort, filter, details) ── JOIN ─> Connecting ─> Lobby
           │          └─ Join by address (name, address, password, recent servers)
           ├─ Settings (Video, Audio, Controls, Bindings, Game, Profile)
           ├─ Credits (licences from docs/ASSETS.md)
           └─ Quit (confirm)
Lobby ─ teams, chat, match settings (host), ready/start, leave (confirm) ─> round ─> results ─> lobby
Match ─ pause menu: resume, settings, leave (confirm) ; end of round: results (offline and online)
```

While the player visits a server the visit's pages (connecting, lobby, travelling, failed) take the screen over
whatever page was open, and give it back when the visit ends.

Unattended runs: `-ScctMenuPage=<page>` opens a page and `-ScctMenuDo="..."` drives the menus (keys, presses,
hovers, screenshots; `Menu/ScctMenuDriver.h`), so every page and state can be pictured with no window on screen.
