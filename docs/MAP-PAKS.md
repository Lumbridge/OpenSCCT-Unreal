# Map paks

Players build stock Versus assets from their Steam installation. The OpenSCCT launcher can download reviewed community map sources and convert them with those local assets for Unreal's runtime map scene.

The IoStore workflow below is for inspecting locally imported maps in a development checkout. Its output belongs in the checkout's `Saved/MapPaks/` folder.

## The cook

```powershell
pwsh Tools/Cook/Cook-MapPaks.ps1               # every imported map but TestRange
pwsh Tools/Cook/Cook-MapPaks.ps1 -Maps AquaE   # one map, for trying the pipeline out
```

It builds the editor and the game, then runs `RunUAT BuildCookRun -cook -stage -pak -iostore` headless with
`-CustomConfig=MapPaks`. That layer (`Config/Custom/MapPaks`) is read only by this cook: it turns chunking on,
clears the release's project-only cook settings (`MapsToCook`, `DirectoriesToAlwaysCook` and `DirectoriesToNeverCook`), and makes `UScctMapPakAssetManager` (`Source/OpenSCCT/Cook`) the
asset manager, which puts every package into exactly one container:

| Container | Holds |
| --- | --- |
| `pakchunk0-Windows` | the engine and this project's own content: the game |
| `pakchunk100-Windows` | the shared pack `imported-shared`: `/Game/Imported/Chars`, `/Game/Imported/Fx`, and anything in a map's folder that another cooked map also uses |
| `pakchunk101-Windows` and up | one per map, in the order of `-Maps`: the level and what only that map uses |

The asset manager also adds every package of the cooked maps' folders and of Chars and Fx to the cook, whether a level
refers to it or not, because the game finds those by looking (a level's meshes and materials, the effect meshes, the
equipment models).

The containers keep the names the cook gives them: the engine reads the chunk number from the file name to open the
container's own shader library when it is mounted.

Output, in `Saved/MapPaks/<cook id>/`:

- `shared/imported-shared/` and `maps/<Code>/`: the `.pak`, `.utoc` and `.ucas` of each pack
- `cook.json`: the cook (id, engine, game version, commit) and each pack's chunk, level, files, sizes and SHA-256
- `chunks/pakchunk<N>.txt`: the cook's list of files in each container
- `uat.log` and the build logs

The script fails when the game's container holds anything imported, when a map's container lacks its level or holds
something from outside its map's folder, or when the cook made a container nobody asked for.

## The cook id

A pack only loads in a game built from the same engine and the same project content it was cooked against. The cook
id `<engine>-<ProjectVersion>-<Revision>` names that build (`-Revision` is bumped for every re-cook against the same
ProjectVersion). Cook the desired maps together so the local chunk assignment can identify their common dependencies.
