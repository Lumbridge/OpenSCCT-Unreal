# Releasing

A release includes a standalone Windows game ZIP, a checksum and metadata for players who update through the launcher.
`Tools/Package-Release.ps1` makes all three. Its header comment lists every file it writes.

## What a release contains

- The game cooks its tracked front end, runtime map, project materials, weapons and third-party art. The setup tool shipped with Unreal prepares retail maps, characters, equipment and sound from the player's Steam installation. The bridge ships the project's Hub and TestBox fixtures.
- The release cook excludes `/Game/Imported`. `Tools/Test/Assert-ReleaseContent.ps1` checks the staged inventories, compares project packages with the tracked source list and hashes the loose fixtures against this repository’s Runtime fixtures. Each successful package leaves `cook-audit.json` alongside the release files.
- The C# bridge builds directly from the source in `Runtime/`. The Windows package also includes its content setup tool, converter and Python runtime under `contenttools/`.
- A private .NET 10 runtime in `Binaries/DotNet`, copied from the newest installed 10.x runtime. Players then need
  no .NET installed. Developers can omit it in a local-only package with `-NoDotNet`.
- `Prerequisites/vc_redist.x64.exe`, the Visual C++ runtime installer from the engine.
- `THIRD-PARTY-NOTICES.md`, copied from the repository root: the third-party components a release ships and their
  licences. Update it when the bridge gains a package or the release gains a component.
- `version.json`, in format 1. It is the Editor's package format with a few more fields: `configuration`,
  `engineVersion`, `openscctVersion`, `openscctCommit`, `executable`, `gameExecutable` and `bundledMaps`. It lists every file with
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
- **Why patches.** The enhanced maps share a cooked `.ucas` of about 1.3 GB. An asset edit can change the
  container's hash and move many already-compressed blocks. On 0.1.2 to 0.1.3 the container's gzip download
  was 1,230 MB. Matching its blocks produced a 9 MB patch and a 22 MB total launcher download.
  Earlier builds with a smaller container gave the following measurements:

  | File | Gzipped blob | Patch |
  |---|---|---|
  | `OpenSCCTUnreal-Win64-Shipping.exe` (160 MB) | 66 MB | 6.8 MB |
  | `OpenSCCTUnreal-Windows.ucas` (500 MB) | 494 MB | 0.8 MB |
  | `.pak`, `global.ucas`, bridge DLL | 10.8 MB | 0.2 MB |

  That is about 8 MB instead of about 571 MB. Another pair, 17 commits of code changes and no content changes, came
  to 7.8 MB with patches and 68 MB without, with the `.ucas` unchanged. A patch takes about 5 minutes to make for the `.ucas` and 1.5
  minutes for the exe, up to four at a time, and about a second to apply.
- **The patch format** is `Tools/ReleaseDelta`. For ordinary files up to 256 MiB it uses bsdiff's suffix-array
  matcher. For larger single-partition UE 5.8 `.ucas` files, `diff-iostore` reads each version's companion
  `.utoc` and matches compressed blocks by SHA-256. Missing or unsupported tables fall back to the file's blob.
  Both methods write an `OSCTDIF1` container of three zlib streams (control,
  diff, extra), with both files' sizes and SHA-256 in the header. The format is described in `Program.cs`.
  `ReleaseDelta apply` is the reference implementation that the launcher's version must match: it reads the old file
  at random positions and writes the new file in order. `ReleaseDelta selftest` checks the suffix array against a
  plain sort and runs diff and apply round trips. The release script runs it first.
- **Stable builds.** The game is always built from the same drive letter (`-BuildDrive`, default `U`), and the
  shader library is written in hash order (`bDeterministicShaderCodeOrder`). Both keep unchanged parts byte-identical,
  so they cost nothing to update.
- **Where blobs and patches live.** A store release shared by every version (`-StoreTag`, default
  `unreal-files-2`). `publish.ps1` uploads only the files the store does not already have, and stops before the
  store would pass GitHub's limit of 1000 assets per release. Advance to a new numbered store when needed.
  A pre-migration base can supply executable deltas. The first Steam-based release writes a complete clean set
  of blobs and replaces the legacy cooked containers as whole files. Each version's file list and checksum also
  go on the `unreal-files-1` metadata release used by existing launchers and on the version release itself.

Keep each published release's output folder under `Saved/Release/<version>`. The next release makes its patches
from those files. `publish.ps1` writes `.published` into the folder once its uploads have succeeded, and a release
takes only folders marked that way as bases, so it never points at blobs that are not on GitHub. A `-Local` package
gets a `.local` marker instead, and only another local package uses it as a base, for trying updates out. The pack
of small files is named by its contents as well as its version, so packaging a version again never collides with a
pack the store already has.

## Simulation and dedicated servers

The Unreal client and its dedicated server build from the same simulation source in `Runtime/`.
The source was copied from OpenSCCT revision `647078c3559acc4b415c905eee367aee2c21c248` and is maintained in this repository.
The release manifest records the Unreal commit and the Git tree of its runtime. The network handshake checks protocol compatibility.

Run `pwsh Tools/Package-DedicatedServer.ps1 -Version <Unreal version> -Out Saved/Release/<Unreal version>`
to produce the Linux and Windows standalone server archives and their checksums. See [dedicated server setup](../Deployment/Dedicated/README.md).

## Procedure

1. Bump `ProjectVersion` in `Config/DefaultGame.ini` and commit. Push to `origin/main`.
2. Pull the Git LFS assets with `git lfs pull`.
3. Initialize the pinned glTFRuntime plugin with `git submodule update --init --recursive` and run the script. It needs Unreal Engine 5.8 (`-Engine` or `UE_ROOT`, default `F:\UE_5.8`), the .NET 10 SDK, a
   clean tree and an unused tag:

   ```powershell
   pwsh Tools/Package-Release.ps1
   ```

   The build and cook take a long time. UAT's log is `Saved/Release/<version>/uat.log`.
4. Try the extracted `OpenSCCT Unreal` folder on a clean profile. Run `OpenSCCTUnreal.exe`, use the built-in Steam content setup and join a match with the packaged server.
5. Tag `v<version>`, edit `notes.md`, and run `publish.ps1`. It makes a **draft** release, which nobody sees until
   it is published on GitHub.

The script builds the game from a free drive letter mapped to the checkout with `subst`, so source paths compiled
into the exe read `X:\Source\...` and never include the builder's user name or checkout path. Afterwards it
removes the mapping. The editor, which only the cook uses, is built from the checkout as usual. The script still
fails if the checkout's path, or the user profile path, is found in any shipped program or text file.

`-Local` makes a package for trying out on this machine. It skips the clean-tree, pushed and tag checks, allows a
Development build (`-Configuration Development`), and only warns about private paths and size. Never upload a local
package.

## Provenance of the source assets

Before anything is built, the script reads every `.uasset` and `.umap` under `Content/` except `Content/Imported`
(everything else may be cooked) as Latin-1 and as UTF-16LE text. An asset imported from a file keeps that file's
path, so the script fails if any of them names a path into a `.content`, `svmap`, `shared/chars` or `shared/maps`
folder (either separator) or an `SPersoTextures_` file: those come from the original game's converted content. It
also fails on a Git LFS pointer file (`version https://git-lfs...`), which means `git lfs pull` was not run and
the asset cannot be checked. With `-Local` both only warn. `-CheckContentOnly` runs this check alone and stops:

```powershell
pwsh Tools/Package-Release.ps1 -Local -CheckContentOnly
```

## Where releases are published

`-Repository` defaults to `OpenSCCT/OpenSCCT-Unreal`, this repository, which is public, as it must be: players'
launchers read releases without a token. `-Repository ''` writes no `publish.ps1`.

## The launcher side

The OpenSCCT launcher lists Unreal under Modernisations (`UnrealService` in OpenSCCT's
`src/OpenScct.Launcher.Core/Unreal/UnrealService.cs`). It installs a release from its `launcher.json` and file list
through the delta installer, which already applies the binary patches from the store release.

## Not decided yet

- **Code signing.** Nothing is signed.
