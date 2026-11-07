#!/usr/bin/env python3
"""Builds evil_ryu_hd_palette.bin (Evil Ryu palettes for the HD sprite sheets) from the user's own game data.

The output is derived from the user's game files and belongs in the game data folder. It must not be committed or
redistributed.

HD Ryu shares its art and pen layout with USF2's chr01, whose battle palettes equal the palette embedded in the HD
sheets, so USF2's chr18 battle palettes apply to the HD art unchanged. The guest stores 256 entries of A, B, G, R bytes.

Layout (little endian header): "SSF2ERH1", u32 count, then count x 1024 bytes. Entries 0..9 are per HD colour byte;
entry 10 is the second player's palette in an Evil Ryu mirror match.
"""
import argparse
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

MAGIC = b"SSF2ERH1"
BATTLE_FOLDER = "/1/nativeNXx64/app/arc_cmn/btl"
ARCHIVE = "n_chr18.arc"
COLOURS = 10
# Every HD colour shows the dark gi and red headband that identify the fighter; the guest's colour choice would
# otherwise turn him red, white or green. Set this to AUTHENTIC_COLOURS for USF2's full colour set.
DARK_COLOUR = 1
MIRROR_COLOUR = 7
AUTHENTIC_COLOURS = [3, 4, 5, 6, 7, 8, 9, 10, 1, 2]
HD_COLOURS = [DARK_COLOUR] * COLOURS
COLOUR_KEY = bytes((0xFF, 0xFF, 0x00, 0xFF))


def fail(message):
    raise SystemExit("error: " + message)


def find_nstool():
    import os

    base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    for pattern in ("capcom/switch-helpers/*/*/bin/nstool*", "capcom/switch-helpers/*/bin/nstool*"):
        found = sorted(base.glob(pattern))
        if found:
            return found[0]
    fail("nstool not found; pass --nstool")


def read_arc(path):
    data = Path(path).read_bytes()
    magic, version, count = struct.unpack_from("<4sHH", data, 0)
    if magic != b"ARC\0" or version != 7:
        fail("unsupported archive " + str(path))
    entries = {}
    for i in range(count):
        at = 8 + i * 0x50
        name = data[at:at + 64].split(b"\0")[0].decode("latin-1").replace(chr(92), "/")
        _, packed, flags, offset = struct.unpack_from("<IIII", data, at + 64)
        size = flags & 0x1FFFFFFF
        blob = data[offset:offset + packed]
        entries[name] = zlib.decompress(blob) if packed != size else blob
    return entries


def guest_palette(entries, colour):
    name = "app/chr/chr18/n/pal/chr18_pal%02d" % colour
    pal = entries.get(name)
    if pal is None:
        fail("missing " + name)
    if pal[:4] != b"PAL\0" or struct.unpack_from("<I", pal, 8)[0] != 256 or len(pal) != 16 + 256 * 16:
        fail("unsupported PAL layout")
    out = bytearray(COLOUR_KEY)
    for i in range(1, 256):
        r, g, b, a = (max(0, min(255, round(v * 255))) for v in struct.unpack_from("<4f", pal, 16 + 16 * i))
        out += bytes((a, b, g, r))
    return bytes(out)


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=root / "assets/characters/evil_ryu_hd_palette.bin")
    parser.add_argument("--xci", type=Path)
    parser.add_argument("--keys", type=Path)
    parser.add_argument("--kit", type=Path, default=root / "game/references/usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    parser.add_argument("--arc-dir", type=Path, help="use an already extracted btl folder instead of the XCI")
    args = parser.parse_args()
    if args.arc_dir is None and not (args.xci and args.keys):
        fail("pass --arc-dir, or --xci and --keys")

    with tempfile.TemporaryDirectory(prefix="evil-ryu-hd-palette-") as work:
        arc_dir = args.arc_dir
        if arc_dir is None:
            sys.path.insert(0, str(args.kit))
            import switch_source

            pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
            nstool = args.nstool or find_nstool()
            with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
                switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(BATTLE_FOLDER),
                                      Path(work) / "btl")
            arc_dir = Path(work) / "btl"
        entries = read_arc(arc_dir / ARCHIVE)

    palettes = [guest_palette(entries, colour) for colour in HD_COLOURS + [MIRROR_COLOUR]]
    payload = MAGIC + struct.pack("<I", len(palettes)) + b"".join(palettes)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(payload)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
