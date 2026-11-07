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
# Outer band the game's roster frame covers at runtime.
FRAME = 6
INNER = SIZE - 2 * FRAME
GUI_FOLDER = "/1/nativeNXx64/app/arc_cmn/gui"
SELECT_ARCHIVE = "gui_chr_sel.arc"
SELECT_LAYOUT = "app\\ui\\battle\\characterselect\\character_select"
FACE_ARCHIVE = "gui_def_face.arc"
# Runtime id, cell in USF2's Character Select layout.
PORTRAITS = ((0, "tex_face18"), (1, "tex_face19"))
PREVIEW_CELLS = (("evil_ryu", "tex_face18"), ("violent_ken", "tex_face19"), ("ryu", "tex_face01"), ("ken", "tex_face05"),
                 ("sagat", "tex_face10"), ("bison", "tex_face09"))
# The 128 px texture is shown in this roster cell, so the art is packed for this anisotropy.
CELL_WIDTH, CELL_HEIGHT = 80, 92
# The black backing of the native portraits: alpha by two-row scanline, then by distance from the frame.
SCANLINE_ALPHA = (117, 94)
EDGE_SHADOW_ALPHA = (242, 177, 159, 145, 132, 120, 104)


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


def select_cell(layout, name):
    """Returns the texture path and the (x0, y0, x1, y1) rect a Character Select cell shows at frame 0."""
    objects, params, keyed, textures, strings, keys, indices, values = 3, 5, 6, 18, 25, 26, 28, 29
    if layout[:4] != b"GUI\0":
        fail("not a GUI layout")

    def u32(at):
        return struct.unpack_from("<I", layout, at)[0]

    def section(index):
        return struct.unpack_from("<Q", layout, 0xA0 + index * 8)[0]

    def text(offset):
        start = section(strings) + offset
        return layout[start:layout.find(b"\0", start)].decode("latin-1")

    def rect_at(offset):
        return struct.unpack_from("<4f", layout, section(values) + offset)

    owner = next((at for at in range(section(objects), section(objects + 1), 0x38) if text(u32(at + 0x10)) == name), None)
    if owner is None:
        fail("missing layout cell " + name)
    found = {}
    for index in range(u32(owner + 0x20), (section(params + 1) - section(params)) // 0x20):
        at = section(params) + index * 0x20
        label = text(u32(at + 0x10))
        if label in found:
            break
        found[label] = u32(at + 0x18)
    rect = rect_at(found["mTextureRect"])
    # A keyed rect replaces the initial value; several keys may share a frame and the last one wins.
    for at in range(section(keyed), section(keyed + 1), 0x28):
        if u32(at + 0xC) != u32(owner) or text(u32(at + 0x10)) != "mTextureRect":
            continue
        for key in range((u32(at + 8) >> 8) & 0xFF):
            if u32(section(keys) + (u32(at + 0x18) + key) * 16) & 0xFFFFFF == 0:
                rect = rect_at(u32(at + 0x20) + key * 16)
    texture = u32(section(indices) + found["Texture"])
    for at in range(section(textures), section(textures + 1), 0x40):
        if u32(at) == texture:
            return text(u32(at + 0x30)), rect
    fail("missing layout texture for " + name)


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


def backing_alpha(x, y):
    alpha = SCANLINE_ALPHA[(y >> 1) & 1]
    edge = min(x, y, SIZE - 1 - x, SIZE - 1 - y) - FRAME
    if 0 <= edge < len(EDGE_SHADOW_ALPHA):
        alpha = max(alpha, EDGE_SHADOW_ALPHA[edge])
    return alpha


def official_cell(face, rect):
    """The image the layout shows: x0 > x1 is how USF2 mirrors the art."""
    from PIL import Image

    x0, y0, x1, y1 = (int(value) for value in rect)
    left, right = min(x0, x1), max(x0, x1)
    if left < 0 or y0 < 0 or left == right or y0 >= y1 or right > face.width or y1 > face.height:
        fail("layout rect outside the texture")
    cell = face.crop((left, y0, right, y1))
    return cell.transpose(Image.FLIP_LEFT_RIGHT) if x0 > x1 else cell


def fitted_size(width, height):
    """The largest texture size inside INNER that keeps width:height once the roster cell stretches it."""
    scale = min(INNER * CELL_WIDTH / width, INNER * CELL_HEIGHT / height) / SIZE
    return round(width * scale * SIZE / CELL_WIDTH), round(height * scale * SIZE / CELL_HEIGHT)


def build_art(cell):
    from PIL import Image

    width, height = fitted_size(*cell.size)
    art = cell.resize((width, height), Image.LANCZOS)
    backing = bytes(backing_alpha(px, py) for py in range(SIZE) for px in range(SIZE))
    runtime = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    runtime.putalpha(Image.frombytes("L", (SIZE, SIZE), backing))
    runtime.alpha_composite(art, ((SIZE - width) // 2, (SIZE - height) // 2))
    return runtime


def load_cell(gui_directory, name):
    gui_directory = Path(gui_directory)
    texture, rect = select_cell(read_arc(gui_directory / SELECT_ARCHIVE)[SELECT_LAYOUT], name)
    entries = read_arc(gui_directory / FACE_ARCHIVE)
    if texture not in entries:
        fail("missing " + texture)
    return official_cell(decode_tex(entries[texture]), rect)


def build_records(gui_directory):
    return {portrait_id: build_art(load_cell(gui_directory, name)) for portrait_id, name in PORTRAITS}


def write_previews(gui_directory, directory, art):
    from PIL import Image

    directory.mkdir(parents=True, exist_ok=True)
    for name, cell in PREVIEW_CELLS:
        load_cell(gui_directory, cell).save(directory / (name + "_official_small.png"))

    shade = (40, 40, 48, 255)
    sheet = Image.new("RGBA", (16 + len(PORTRAITS) * 512, 300), shade)
    for column, (portrait_id, name) in enumerate(PORTRAITS):
        official = Image.new("RGBA", (240, 276), shade)
        official.alpha_composite(load_cell(gui_directory, name).resize((228, 272), Image.LANCZOS), (6, 2))
        runtime = Image.new("RGBA", (SIZE, SIZE), shade)
        runtime.alpha_composite(art[portrait_id])
        sheet.paste(official, (12 + column * 512, 12))
        sheet.paste(runtime.resize((CELL_WIDTH * 3, CELL_HEIGHT * 3), Image.LANCZOS), (264 + column * 512, 12))
    sheet.save(directory / "official_vs_runtime.png")


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xci", required=True, type=Path, help="USF2 USA XCI or NSP")
    parser.add_argument("--keys", required=True, type=Path, help="prod.keys")
    parser.add_argument("--out", required=True, type=Path, help="ultra_portraits.bin to write")
    parser.add_argument("--kit", type=Path, default=root / "game/references/usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    parser.add_argument("--preview-dir", type=Path, help="also write PNG previews here; keep them out of the repository")
    args = parser.parse_args()

    sys.path.insert(0, str(args.kit))
    import switch_source

    pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
    nstool = args.nstool or find_nstool()
    with tempfile.TemporaryDirectory(prefix="ultra-portraits-") as work:
        with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
            gui = Path(work) / "gui"
            switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(GUI_FOLDER), gui)
            art = build_records(gui)
            if args.preview_dir:
                write_previews(gui, args.preview_dir, art)
    records = [struct.pack("<3I", portrait_id, SIZE, SIZE) + image.tobytes() for portrait_id, image in art.items()]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(MAGIC + struct.pack("<I", len(records)) + b"".join(records))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
