#!/usr/bin/env python3
"""Builds ultra_portraits.bin (Evil Ryu and Violent Ken roster art) from the user's own USF2 dump.

The output is derived from the user's game data and belongs next to the HD Remix game files. It must not be
committed or redistributed.
"""
import argparse
import io
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

MAGIC = b"SSF2UPT1"
SIZE = 128
GUI_FOLDER = "/1/nativeNXx64/app/arc_cmn/gui"
FACE_ARCHIVE = "gui_def_face.arc"
# id, face entry, square crop (x, y, size) matching USF2's own charaicon framing.
PORTRAITS = (
    (0, "app\\ui\\battle\\face\\chara18_01_ID", (34, 58, 376)),
    (1, "app\\ui\\battle\\face\\chara19_01_ID", (80, 70, 358)),
)


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


def deswizzle(data, width_blocks, height_blocks, block_bytes, block_height):
    gobs_wide = (width_blocks * block_bytes + 63) // 64
    out = bytearray(width_blocks * height_blocks * block_bytes)
    for y in range(height_blocks):
        for x in range(width_blocks):
            xb = x * block_bytes
            gob = ((y // (8 * block_height)) * 512 * block_height * gobs_wide
                   + (xb // 64) * 512 * block_height + ((y % (8 * block_height)) // 8) * 512)
            at = (gob + ((xb % 64) // 32) * 256 + ((y % 8) // 2) * 64 + ((xb % 32) // 16) * 32
                  + (y % 2) * 16 + (xb % 16))
            out[(y * width_blocks + x) * block_bytes:(y * width_blocks + x + 1) * block_bytes] = data[at:at + block_bytes]
    return bytes(out)


def decode_tex(tex):
    from PIL import Image

    if tex[:4] != b"TEX\0":
        fail("not a TEX texture")
    fields = struct.unpack_from("<I", tex, 8)[0]
    width, height = (fields >> 6) & 0x1FFF, (fields >> 19) & 0x1FFF
    if fields & 0x3F != 1 or len(tex) != 24 + width * height:
        fail("unsupported TEX layout (expected one 8 bpp BC7 level)")
    wb, hb = width // 4, height // 4
    block_height = min(16, max(1, 1 << ((hb // 8 - 1).bit_length())))
    blocks = deswizzle(tex[24:], wb, hb, 16, block_height)
    header = b"DDS " + struct.pack("<7I", 124, 0x1007 | 0x80000, height, width, len(blocks), 0, 1) + b"\0" * 44
    header += struct.pack("<2I4s5I", 32, 4, b"DX10", 0, 0, 0, 0, 0) + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    header += struct.pack("<5I", 98, 3, 0, 1, 0)
    return Image.open(io.BytesIO(header + blocks)).convert("RGBA")


def build_art(face, crop):
    from PIL import Image

    x, y, size = crop
    art = face.crop((x, y, x + size, y + size)).resize((SIZE, SIZE), Image.LANCZOS)
    flat = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 255))
    flat.alpha_composite(art)
    return flat


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xci", required=True, type=Path, help="USF2 USA XCI or NSP")
    parser.add_argument("--keys", required=True, type=Path, help="prod.keys")
    parser.add_argument("--out", required=True, type=Path, help="ultra_portraits.bin to write")
    parser.add_argument("--kit", type=Path, default=root / "game/references/usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    args = parser.parse_args()

    sys.path.insert(0, str(args.kit))
    import switch_source

    pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
    nstool = args.nstool or find_nstool()
    with tempfile.TemporaryDirectory(prefix="ultra-portraits-") as work:
        with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
            switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(GUI_FOLDER), Path(work) / "gui")
            entries = read_arc(Path(work) / "gui" / FACE_ARCHIVE)
    records = []
    for portrait_id, name, crop in PORTRAITS:
        if name not in entries:
            fail("missing " + name)
        art = build_art(decode_tex(entries[name]), crop)
        records.append(struct.pack("<3I", portrait_id, SIZE, SIZE) + art.tobytes())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(MAGIC + struct.pack("<I", len(records)) + b"".join(records))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
