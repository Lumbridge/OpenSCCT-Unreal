# Releasing

A release is a zip that the OpenSCCT launcher installs, plus a checksum and a launcher handshake file.
`Tools/Package-Release.ps1` makes all three. Its header comment lists every file it writes.

## What a release contains

- The game is a Shipping Win64 build. Its starter is `OpenSCCTUnreal.exe` and the game itself is
  `OpenSCCTUnreal/Binaries/Win64/OpenSCCTUnreal-Win64-Shipping.exe`. The game target is named `OpenSCCTUnreal` so
  that the launcher, which finds a running Godot game by the process name `OpenSCCT`, never mistakes the Unreal game
  for it.
- This project's own content, cooked: the front end, materials, the merc rifle and the CC0 texture sets.
  `Config/DefaultGame.ini` lists the front end in `MapsToCook` and always cooks `/Game/OpenSCCT`.
- **No original game content.** `/Game/Imported` is in `DirectoriesToNeverCook`. The script fails if staging's
  list of packed files (`Manifest_UFSFiles_Win64.txt`) names anything under `Content/Imported`. The original content includes every playable map, the hub, the characters, the weapons
  and the effects. So for now a release starts the front end and can do nothing more. Playing needs one of two
  things: cooked map packs mounted from `%LOCALAPPDATA%\OpenSCCT\unreal-packs` (the
  [OpenSCCT-Unreal-Content](https://github.com/Lumbridge/OpenSCCT-Unreal-Content) format), or a change to that rule.
- The C# bridge, built against the OpenSCCT checkout (`-OpenScct`, default `..\OSC-unreal-bridge`).
- A private .NET 10 runtime in `Binaries/DotNet`, copied from the newest installed 10.x runtime. Players then need
  no .NET installed. `-NoDotNet` leaves it out.
- `Prerequisites/vc_redist.x64.exe`, the Visual C++ runtime installer from the engine.
- `version.json`, in format 1. It is the Editor's package format with a few more fields: `configuration`,
  `engineVersion`, `openscctVersion`, `openscctCommit`, `executable` and `gameExecutable`. It lists every file with
  its size and SHA-256.

## Updates: what a player downloads

The zip is for a fresh install. Updates use `unreal-files\`, which follows the launcher's delta format for the
Godot game (`BuildFiles` and `DeltaInstaller` in OpenSCCT's launcher):

- **The file list.** `OpenSCCT-Unreal-<version>-win-x64-files.json`, format 1, lists every file with its size,
  SHA-256 and blob. A file that some base release already published keeps pointing at that blob. A new file of
  256 KB or more becomes `<sha256>.gz`, and smaller ones share
  `OpenSCCT-Unreal-<version>-win-x64-pack-<hash>.zip`. The
  launcher keeps any file the player already has and downloads the rest.
- **Patches.** A file of 1 MB or more whose contents changed since a base release (`-Base`, by default the two
  newest older versions under `Saved/Release`) also gets `<old sha256>-<new sha256>.osdiff`, a binary patch from the
  base's version of the file. It is listed under the file as
  `"patches": [{ "from", "fromSize", "tag", "name", "size" }]`. A patch is kept only when it is under half the
  gzipped file. The script applies every patch once and checks the result's SHA-256 before it lists the patch.
  Launchers that don't know about patches ignore the field and download the blob.
- **Why patches.** Cooked content is one 500 MB `.ucas`. When no asset changes, a recook is byte-identical and the
  file costs nothing to update. One changed asset, though, shifts everything after it in the container, and the
  exe changes with any code change. So per-file deltas alone would download the whole `.ucas` for any content
  change, and 66 MB for any code change. Measured on two real builds 16 commits apart, with about 16 changed or new
  assets and code changes:

  | File | Gzipped blob | Patch |
  |---|---|---|
  | `OpenSCCTUnreal-Win64-Shipping.exe` (160 MB) | 66 MB | 6.8 MB |
  | `OpenSCCTUnreal-Windows.ucas` (500 MB) | 494 MB | 0.8 MB |
  | `.pak`, `global.ucas`, bridge DLL | 10.8 MB | 0.2 MB |

  That is about 8 MB instead of about 571 MB. Another pair, 17 commits of code changes and no content changes, came
  to 7.8 MB with patches and 68 MB without, with the `.ucas` unchanged. A patch takes about 5 minutes to make for the `.ucas` and 1.5
  minutes for the exe, up to four at a time, and about a second to apply.
- **The patch format** is `Tools/ReleaseDelta`. It uses bsdiff's matcher on a suffix array, which finds code whose
  addresses moved and content blocks that moved. It writes an `OSCTDIF1` container of three zlib streams (control,
  diff, extra), with both files' sizes and SHA-256 in the header. The format is described in `Program.cs`.
  `ReleaseDelta apply` is the reference implementation that the launcher's version must match: it reads the old file
  at random positions and writes the new file in order. `ReleaseDelta selftest` checks the suffix array against a
  plain sort and runs diff and apply round trips. The release script runs it first.
- **Stable builds.** The game is always built from the same drive letter (`-BuildDrive`, default `U`), and the
  shader library is written in hash order (`bDeterministicShaderCodeOrder`). Both keep unchanged parts byte-identical,
  so they cost nothing to update.
- **Where blobs and patches live.** A store release shared by every version (`-StoreTag`, default
  `unreal-files-1`). `publish.ps1` uploads only the files the store does not already have, and stops before the
  store would pass GitHub's limit of 1000 assets per release. When it does, start `unreal-files-2`.

Keep each published release's output folder under `Saved/Release/<version>`. The next release makes its patches
from those files. `publish.ps1` writes `.published` into the folder once its uploads have succeeded, and a release
takes only folders marked that way as bases, so it never points at blobs that are not on GitHub. A `-Local` package
gets a `.local` marker instead, and only another local package uses it as a base, for trying updates out. The pack
of small files is named by its contents as well as its version, so packaging a version again never collides with a
pack the store already has.

## Which servers it can join

The bridge speaks OpenSCCT's protocol, and the protocol hash is computed from OpenSCCT's code. A release therefore
joins only servers built from the same OpenSCCT code as its bridge. Before packaging, check out the commit the live
servers run in the OpenSCCT checkout. The script records that commit and version in `version.json`, in the launcher
handshake and in the release notes. It refuses an OpenSCCT checkout with uncommitted changes (`-Local` only warns).

## Procedure

1. Bump `ProjectVersion` in `Config/DefaultGame.ini` and commit. Push to `origin/main`.
2. Check out the live servers' OpenSCCT commit in the bridge checkout.
3. Run the script. It needs Unreal Engine 5.8 (`-Engine` or `UE_ROOT`, default `F:\UE_5.8`), the .NET 10 SDK, a
   clean tree and an unused tag:

   ```powershell
   pwsh Tools/Package-Release.ps1 -Repository <owner/name>
   ```

   The build and cook take a long time. UAT's log is `Saved/Release/<version>/uat.log`.
4. Try the extracted `OpenSCCT Unreal` folder on a machine without the engine.
5. Tag `v<version>`, edit `notes.md`, and run `publish.ps1`. It makes a **draft** release, which nobody sees until
   it is published on GitHub.

The script builds the game from a free drive letter mapped to the checkout with `subst`, so source paths compiled
into the exe read `X:\Source\...` and never include the builder's user name or checkout path. Afterwards it
removes the mapping. The editor, which only the cook uses, is built from the checkout as usual. The script still
fails if the checkout's path, or the user profile path, is found in any shipped program or text file.

`-Local` makes a package for trying out on this machine. It skips the clean-tree, pushed and tag checks, allows a
Development build (`-Configuration Development`), and only warns about private paths and size. Never upload a local
package.

## Not decided yet

- **Where releases are published.** This repository is private, and players' launchers read releases without a
  token, so releases need a public repository. Until one is chosen, `-Repository` has no default and no
  `publish.ps1` is written without it.
- **The launcher side.** The launcher does not read these releases yet. The plan is an Unreal tab built like the
  Editor's (`EditorService` in OpenSCCT's `src/OpenScct.Launcher.Core/Editor`). It would start the game with
  `-ScctContent=<packs folder>` and update through the game's delta installer, which first needs to learn to apply
  `patches`. Until then, a launcher updating from the file list downloads changed files whole.
- **Code signing.** Nothing is signed.
