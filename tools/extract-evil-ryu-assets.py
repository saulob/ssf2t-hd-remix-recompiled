#!/usr/bin/env python3
"""Builds evil_ryu_assets.bin (Evil Ryu Character Select art) from the user's own USF2 dump.

The output is derived from the user's game data and belongs next to the HD Remix game files. It must not be
committed or redistributed.

Records (little endian): u32 id, u32 width, u32 height, RGBA8 pixels (straight alpha, top-down rows).
  id 1  large portrait: USF2 gui_sel_face chara18_01_ID cover-cropped to the guest's portrait quad aspect
        (96x96 arcade = 236.4x288 px at 720p, i.e. 0.8208), 2x the quad height, art facing right as in USF2.
  id 2  name graphic: "EVIL RYU" gold gradient line from gui_chr_sel name02_ID, tight alpha bounding box.
"""
import argparse
import io
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

MAGIC = b"SSF2ERA1"
GUI_FOLDER = "/1/nativeNXx64/app/arc_cmn/gui"
FACE_ARCHIVE = "gui_sel_face.arc"
FACE_ENTRY = "app\\ui\\battle\\face\\chara18_01_ID"
NAME_ARCHIVE = "gui_chr_sel.arc"
NAME_ENTRY = "app\\ui\\battle\\characterselect\\name02_ID"
PORTRAIT_HEIGHT = 576
PORTRAIT_ASPECT = 96.0 * (19.0 / 12.0) * (14.0 / 9.0) / (96.0 * 2.0 * 1.5)
NAME_PROBE_Y = 555  # a row inside the gold EVIL RYU line of name02_ID


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
    """USF2 TEX: 24 byte header, one 8 bpp BC7 level, Tegra block-linear. Height padding to the GOB grid is allowed."""
    from PIL import Image

    if tex[:4] != b"TEX\0":
        fail("not a TEX texture")
    fields = struct.unpack_from("<I", tex, 8)[0]
    width, height = (fields >> 6) & 0x1FFF, (fields >> 19) & 0x1FFF
    wb, hb = (width + 3) // 4, (height + 3) // 4
    block_height = 1
    while block_height < (hb + 7) // 8:
        block_height *= 2
    block_height = min(16, block_height)
    if fields & 0x3F != 1 or len(tex) < 24 + wb * hb * 16:
        fail("unsupported TEX layout (expected one 8 bpp BC7 level)")
    blocks = deswizzle(tex[24:], wb, hb, 16, block_height)
    header = b"DDS " + struct.pack("<7I", 124, 0x1007 | 0x80000, hb * 4, wb * 4, len(blocks), 0, 1) + b"\0" * 44
    header += struct.pack("<2I4s5I", 32, 4, b"DX10", 0, 0, 0, 0, 0) + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    header += struct.pack("<5I", 98, 3, 0, 1, 0)
    return Image.open(io.BytesIO(header + blocks)).convert("RGBA").crop((0, 0, width, height))


def build_portrait(face):
    from PIL import Image

    bounds = face.getchannel("A").getbbox()
    if not bounds:
        fail("empty portrait")
    top, bottom = bounds[1], bounds[3]
    height = bottom - top
    width = int(round(height * PORTRAIT_ASPECT))
    left = max(0, (face.width - width) // 2)
    art = face.crop((left, top, min(face.width, left + width), bottom))
    out_height = PORTRAIT_HEIGHT
    out_width = int(round(out_height * PORTRAIT_ASPECT))
    return art.resize((out_width, out_height), Image.LANCZOS)


def build_name(names):
    from PIL import Image

    pixels = names.load()
    gold = Image.new("L", names.size, 0)
    mask = gold.load()
    for y in range(names.height):
        for x in range(names.width):
            r, _, _, a = pixels[x, y]
            if a > 128 and r > 150:
                mask[x, y] = 255
    rows = [y for y in range(names.height) if gold.crop((0, y, names.width, y + 1)).getbbox()]
    start = end = None
    for y in rows:
        if start is None:
            start = end = y
        elif y == end + 1:
            end = y
        else:
            if start <= NAME_PROBE_Y <= end:
                break
            start = end = y
    if start is None or not start <= NAME_PROBE_Y <= end:
        fail("EVIL RYU name line not found")
    line = gold.crop((0, start, names.width, end + 1)).getbbox()
    box = (line[0], start, line[2], end + 1)
    return names.crop(box)


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xci", required=True, type=Path, help="USF2 USA XCI or NSP")
    parser.add_argument("--keys", required=True, type=Path, help="prod.keys")
    parser.add_argument("--out", required=True, type=Path, help="evil_ryu_assets.bin to write")
    parser.add_argument("--kit", type=Path, default=root / "game/references/usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    parser.add_argument("--preview-dir", type=Path, help="optional folder for PNG previews of the records")
    args = parser.parse_args()

    sys.path.insert(0, str(args.kit))
    import switch_source

    pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
    nstool = args.nstool or find_nstool()
    with tempfile.TemporaryDirectory(prefix="evil-ryu-assets-") as work:
        with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
            switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(GUI_FOLDER), Path(work) / "gui")
            faces = read_arc(Path(work) / "gui" / FACE_ARCHIVE)
            names = read_arc(Path(work) / "gui" / NAME_ARCHIVE)
    if FACE_ENTRY not in faces:
        fail("missing " + FACE_ENTRY)
    if NAME_ENTRY not in names:
        fail("missing " + NAME_ENTRY)
    images = [(1, build_portrait(decode_tex(faces[FACE_ENTRY]))), (2, build_name(decode_tex(names[NAME_ENTRY])))]
    records = [struct.pack("<3I", record_id, image.width, image.height) + image.tobytes() for record_id, image in images]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(MAGIC + struct.pack("<I", len(records)) + b"".join(records))
    if args.preview_dir:
        args.preview_dir.mkdir(parents=True, exist_ok=True)
        for record_id, image in images:
            image.save(args.preview_dir / ("evil_ryu_record_%d.png" % record_id))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
