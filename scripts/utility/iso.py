"""Build an xdelta patch from a vanilla ISO plus a folder of replacement files.

The Makefile's `patch` target calls this with out/files staged as the overlay.
Extraction and rebuild go through .temp/ and out.iso in the working directory;
out.iso is left in place so it can be booted directly.
"""

import os
import shutil
import subprocess
import sys
from argparse import ArgumentParser
from pathlib import Path

from pyisotools.iso import GamecubeISO


def main(argv):
    parser = ArgumentParser(
        "iso.py",
        description="Creates a modified iso from a base iso and root folder "
        "containing additional/modified files.",
        allow_abbrev=False,
    )
    parser.add_argument("src", help="path to vanilla iso")
    parser.add_argument(
        "replace", help="path to folder containing files to replace in the iso"
    )
    parser.add_argument("dest", help="path to outputted .xdelta file")
    args = parser.parse_args(args=argv)

    src_path = Path(args.src).resolve()
    out_path = Path(args.dest).resolve()
    replacement_files_path = Path(args.replace).resolve()
    root_folder_path = Path(".temp").resolve()
    iso_path = Path("out.iso").resolve()

    extract_iso(src_path, root_folder_path)
    copy_all_files(replacement_files_path, root_folder_path / "root")
    build_iso(root_folder_path / "root", iso_path)
    remove(root_folder_path)
    create_xdelta_patch(src_path, iso_path, out_path)


def extract_iso(iso_path, root_folder):
    print("Extracting iso...")
    GamecubeISO.from_iso(iso_path).extract(root_folder, dumpPositions=False)


def build_iso(root_folder, iso_path):
    print("Rebuilding iso...")
    GamecubeISO.from_root(root_folder, genNewInfo=False).build(iso_path)


def copy_all_files(src_folder, dst_folder):
    src_path = Path(src_folder)
    dst_path = Path(dst_folder)
    dst_path.mkdir(parents=True, exist_ok=True)

    print("Copying files...")
    for file in src_path.rglob("*"):
        if file.is_file():
            target = dst_path / file.relative_to(src_path)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)


def create_xdelta_patch(original_iso, modified_iso, patch_file):
    print("Creating patch...")
    result = subprocess.run(
        [
            "xdelta",
            "-B",
            "1363148800",
            "-e",
            "-f",
            "-s",
            str(Path(original_iso).resolve()),
            str(Path(modified_iso).resolve()),
            str(Path(patch_file).resolve()),
        ],
        capture_output=True,
        text=True,
        check=False,
    )

    if result.returncode != 0:
        print("Error creating patch:", result.stderr)
    else:
        print("Patch created:", patch_file)


def remove(path):
    if os.path.isfile(path):
        os.remove(path)
    elif os.path.isdir(path):
        shutil.rmtree(path)


if __name__ == "__main__":
    main(sys.argv[1:])
