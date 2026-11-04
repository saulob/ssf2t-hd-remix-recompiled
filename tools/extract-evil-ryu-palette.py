#!/usr/bin/env python3
"""Builds evil_ryu_palette.bin (Evil Ryu body colours) from the user's own USF2 dump.

The output is derived from the user's game data and belongs next to the HD Remix game files. It must not be
committed or redistributed.

Layout (little endian): "SSF2ERP1", u32 count, then per colour 16 u16 Ryu words followed by 16 u16 Evil Ryu words.
A word is 0x0RGB (4 bits per channel) for USF2 palette entries 17..31; word 15 is the transparent pen (0).
"""
import argparse
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

MAGIC = b"SSF2ERP1"
PREVIEW_FOLDER = "/1/nativeNXx64/app/arc_cmn/preview"
COLOURS = 10
# Every guest colour shows the dark gi and red headband; see tools/extract-evil-ryu-hd-palette.py.
DARK_COLOUR = 0
FIRST_ENTRY = 17
WORDS = 16


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
        name = data[at:at + 64].split(b"\0")[0].decode("latin-1")
        _, packed, flags, offset = struct.unpack_from("<IIII", data, at + 64)
        size = flags & 0x1FFFFFFF
        blob = data[offset:offset + packed]
        entries[name] = zlib.decompress(blob) if packed != size else blob
    return entries


def body_words(pal):
    if pal[:4] != b"PAL\0" or struct.unpack_from("<I", pal, 8)[0] != 256 or len(pal) != 16 + 256 * 16:
        fail("unsupported PAL layout")
    words = []
    for pen in range(WORDS - 1):
        r, g, b, _ = struct.unpack_from("<4f", pal, 16 + (FIRST_ENTRY + pen) * 16)
        words.append((round(r * 15) << 8) | (round(g * 15) << 4) | round(b * 15))
    words.append(0)
    return words


def palettes(arc_path, chara):
    entries = read_arc(arc_path)
    out = []
    for colour in range(1, COLOURS + 1):
        name = r"app\chr\chr%02d\c\pal\chr%02d_pal%02d" % (chara, chara, colour)
        if name not in entries:
            fail("missing " + name)
        out.append(body_words(entries[name]))
    return out


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xci", type=Path)
    parser.add_argument("--keys", type=Path)
    parser.add_argument("--out", type=Path, default=root / "game/extracted/evil_ryu_palette.bin")
    parser.add_argument("--kit", type=Path, default=root / "game/references/usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    parser.add_argument("--arc-dir", type=Path, help="use already extracted preview archives instead of the XCI")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="evil-ryu-palette-") as work:
        arc_dir = args.arc_dir
        if arc_dir is None:
            sys.path.insert(0, str(args.kit))
            import switch_source

            pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
            nstool = args.nstool or find_nstool()
            with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
                switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(PREVIEW_FOLDER),
                                      Path(work) / "preview")
            arc_dir = Path(work) / "preview"
        ryu = palettes(arc_dir / "prev_c_chr01.arc", 1)
        evil = palettes(arc_dir / "prev_c_chr18.arc", 18)
    payload = MAGIC + struct.pack("<I", COLOURS)
    for r in ryu:
        payload += struct.pack("<16H", *r) + struct.pack("<16H", *evil[DARK_COLOUR])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(payload)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
