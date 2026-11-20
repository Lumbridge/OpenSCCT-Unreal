"""Check the actual restores and distribution inputs used by the Unreal edition."""
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "Runtime"
sys.path.insert(0, str(ROOT / "Tools" / "UnrealContent"))
import package as converter


def verify_restore(directory):
    assets = json.loads((directory / "obj" / "project.assets.json").read_text(encoding="utf-8-sig"))
    projects = []
    for key, library in assets["libraries"].items():
        name = key.split("/", 1)[0]
        if name.casefold().startswith("godot"):
            raise ValueError(f"{directory.name} depends on {name}")
        if library["type"] == "project":
            project = (directory / library["path"]).resolve(strict=True)
            if not project.is_relative_to(RUNTIME):
                raise ValueError(f"{directory.name} uses a project outside Runtime: {project}")
            projects.append(project)
    if not projects:
        raise ValueError(f"No local runtime projects in the restore for {directory.name}")
    print(f"{directory.name}: {len(projects)} local runtime projects")


def main():
    bridge = ROOT / "Bridge/OpenScct.UnrealBridge"
    for directory in (bridge, RUNTIME / "src/OpenScct.Server.Cli", ROOT / "Tools/SteamSetup"):
        verify_restore(directory)

    project = bridge / "OpenScct.UnrealBridge.csproj"
    result = subprocess.check_output(["dotnet", "msbuild", str(project), "-nologo", "-getItem:Compile"], cwd=RUNTIME)
    files = [Path(item["FullPath"]).resolve(strict=True) for item in json.loads(result)["Items"]["Compile"]]
    for file in files:
        if not file.is_relative_to(ROOT):
            raise ValueError(f"Bridge compiles a file outside this checkout: {file}")
    print(f"Verified {len(files)} bridge source files")

    # This bank has no redistribution licence. Its text-only provenance records can remain in the snapshot.
    bank = "content/assets/user_gun_audio"
    files = subprocess.check_output(["git", "ls-files", "-z", "--", bank], cwd=RUNTIME)
    for filename in files.decode().split("\0"):
        if filename and Path(filename).suffix.lower() not in {".py", ".txt", ".json", ".md"}:
            raise ValueError(f"Unlicensed bank contains a non-source file: {filename}")

    sources = converter.tracked_sources()
    if any(path.startswith(bank + "/") for path in sources):
        raise ValueError("The standalone converter includes the unlicensed sound bank")
    digest = converter.check_sources(RUNTIME, sources)
    print(f"Verified {len(sources)} converter inputs, recipe {digest}")


if __name__ == "__main__":
    main()
