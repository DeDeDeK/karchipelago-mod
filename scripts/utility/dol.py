"""Extract the main executable (.dol) from a GameCube ISO.

The Makefile calls this to produce externals/hoshi/dol/kar.dol, which hoshi
patches and relinks against.
"""

import sys
from argparse import ArgumentParser
from pathlib import Path

from pyisotools.iso import GamecubeISO


def main(argv):
    parser = ArgumentParser(
        "dol.py", description="Extracts .dol file from iso to a specified location.",
        allow_abbrev=False)
    parser.add_argument("iso", help="path to vanilla iso")
    parser.add_argument("dol", help="path to outputted .dol file")
    args = parser.parse_args(args=argv)

    iso = GamecubeISO.from_iso(Path(args.iso).resolve())
    dol_path = Path(args.dol).resolve()
    dol_path.parent.mkdir(parents=True, exist_ok=True)
    with open(dol_path, "wb") as f:
        iso.dol.save(f)


if __name__ == "__main__":
    main(sys.argv[1:])
