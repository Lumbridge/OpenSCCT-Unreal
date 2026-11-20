"""Check release archives for debug material and machine-local build paths."""

import argparse
from pathlib import Path, PurePosixPath
import tarfile
import zipfile


def local_path_bytes(paths):
    for path in paths:
        local = str(path.resolve())
        if len(local) <= 3:
            continue
        for spelling in {local.replace("\\", "/"), local.replace("/", "\\")}:
            for encoding in ("utf-8", "utf-16-le", "utf-16-be"):
                yield spelling.encode(encoding).lower()


def check_file(name, data, private_paths):
    filename = PurePosixPath(name).name.lower()
    if filename.endswith((".pdb", ".mdb", ".dbg", ".debug", ".sym", ".dwarf")):
        raise ValueError(f"Loose debug symbols: {name}")
    if filename.endswith(".dll") and (filename.startswith("openscct.") or filename == "svm-server.dll"):
        # Only our assemblies are built here. Keep the vendor's signed runtime binaries intact.
        if b"RSDS" in data or b"MPDB" in data:
            raise ValueError(f"Published assembly carries CodeView or embedded PDB data: {name}")
    lower = data.lower()
    if any(path in lower for path in private_paths):
        raise ValueError(f"Machine-local build path in {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("build_path", nargs="*", type=Path)
    args = parser.parse_args()
    paths = tuple(local_path_bytes([Path(__file__).resolve().parents[2], Path.home(), *args.build_path]))
    count = 0
    if zipfile.is_zipfile(args.package):
        with zipfile.ZipFile(args.package) as archive:
            for entry in archive.infolist():
                if not entry.is_dir():
                    check_file(entry.filename, archive.read(entry), paths)
                    count += 1
    else:
        with tarfile.open(args.package, "r|gz") as archive:
            for entry in archive:
                if entry.isfile():
                    check_file(entry.name, archive.extractfile(entry).read(), paths)
                    count += 1
    if not count:
        raise ValueError("Empty dedicated server package")
    print(f"Verified {count} dedicated server files in {args.package.name}")


if __name__ == "__main__":
    main()
