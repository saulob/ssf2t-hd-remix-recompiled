#!/usr/bin/env python3
"""Builds evil_ryu_sprites.bin (Evil Ryu art HD Remix lacks, as guest indexed textures) from the user's own USF2 dump.

Holds the two USF2 fight sprites and, quantized from evil_ryu_assets.bin, the large portrait and the name graphics.
The output is derived from the user's game data and belongs in the game data folder. It must not be committed or
redistributed.

Layout (little endian): "SSF2ERS2", u32 count, then per image u32 id, u32 width, u32 height, u32 has_palette, an
optional 1024 byte A, B, G, R palette, and width * height 8 bit palette indices, top row first. Index 0 is transparent.
Fight sprites have no palette of their own: they use the fighter palette.
"""
import argparse
import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

MAGIC = b"SSF2ERS2"
BATTLE_FOLDER = "/1/nativeNXx64/app/arc_cmn/btl"
ARCHIVE = "n_chr18.arc"
SPRITES = [(224, "app/chr/chr18/n/tex/chr18_200_00"), (225, "app/chr/chr18/n/tex/chr18_201_00")]
TEX_HEADER = 24
BLOCK_HEIGHT = 16
ASSET_MAGIC = b"SSF2ERA1"
ASSET_PORTRAIT = 1
ASSET_NAME = 2
# Guest sizes: ryu_large is 371x416; the name quads are 96x16 and 90x16 and every HD asset is 4x its quad.
PORTRAIT_IMAGE = (1, 371, 416)
NAME_IMAGE = (2, 384, 64)
HUD_NAME_IMAGE = (3, 360, 64)
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
        name = data[at:at + 64].split(b"\0")[0].decode("latin-1")
        _, packed, flags, offset = struct.unpack_from("<IIII", data, at + 64)
        size = flags & 0x1FFFFFFF
        blob = data[offset:offset + packed]
        entries[name] = zlib.decompress(blob) if packed != size else blob
    return entries


def deswizzle(data, width, height):
    gobs_wide = (width + 63) // 64
    out = bytearray(width * height)
    for y in range(height):
        for x in range(width):
            gob = ((y // (8 * BLOCK_HEIGHT)) * 512 * BLOCK_HEIGHT * gobs_wide
                   + (x // 64) * 512 * BLOCK_HEIGHT + ((y % (8 * BLOCK_HEIGHT)) // 8) * 512)
            at = gob + ((x % 64) // 32) * 256 + ((y % 8) // 2) * 64 + ((x % 32) // 16) * 32 + (y % 2) * 16 + (x % 16)
            out[y * width + x] = data[at]
    return bytes(out)


def decode_indices(tex):
    if tex[:4] != b"TEX\0":
        fail("unsupported TEX header")
    packed = struct.unpack_from("<I", tex, 8)[0]
    width, height = (packed >> 6) & 0x1FFF, (packed >> 19) & 0x1FFF
    return width, height, deswizzle(tex[TEX_HEADER:], width, height)


def read_assets(path):
    data = Path(path).read_bytes()
    if data[:8] != ASSET_MAGIC:
        fail("unsupported asset file " + str(path))
    count = struct.unpack_from("<I", data, 8)[0]
    at = 12
    images = {}
    for _ in range(count):
        record, width, height = struct.unpack_from("<3I", data, at)
        at += 12
        images[record] = (width, height, data[at:at + width * height * 4])
        at += width * height * 4
    return images


def fit_canvas(art, width, height, fill):
    """Portraits fill the guest texture edge to edge, as its own portraits do: the quad that shows it is narrower than
    the texture, so art authored at the quad's aspect is stretched to the texture."""
    from PIL import Image

    if fill:
        return art.resize((width, height), Image.LANCZOS)
    scale = min(width / art.width, height / art.height)
    art = art.resize((max(1, round(art.width * scale)), max(1, round(art.height * scale))), Image.LANCZOS)
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    canvas.paste(art, ((width - art.width) // 2, (height - art.height) // 2))
    return canvas


def indexed_record(record, width, height, source, fill=False):
    from PIL import Image

    canvas = fit_canvas(Image.frombytes("RGBA", source[:2], source[2]), width, height, fill)
    alpha = canvas.getchannel("A").point(lambda v: 255 if v >= 128 else 0)
    quantized = canvas.convert("RGB").quantize(colors=255, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    colours = quantized.getpalette()[:255 * 3]
    colours += [0] * (255 * 3 - len(colours))
    palette = bytearray(COLOUR_KEY)
    for i in range(255):
        red, green, blue = colours[i * 3:i * 3 + 3]
        palette += bytes((0xFF, blue, green, red))
    pixels = bytes(0 if a == 0 else p + 1 for p, a in zip(quantized.tobytes(), alpha.tobytes()))
    return struct.pack("<4I", record, width, height, 1) + bytes(palette) + pixels


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=root / "game/extracted/evil_ryu_sprites.bin")
    parser.add_argument("--assets", type=Path, default=root / "game/extracted/evil_ryu_assets.bin")
    parser.add_argument("--xci", type=Path)
    parser.add_argument("--keys", type=Path)
    parser.add_argument("--kit", type=Path, default=root / "game/references/usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    parser.add_argument("--arc-dir", type=Path, help="use an already extracted btl folder instead of the XCI")
    args = parser.parse_args()
    if args.arc_dir is None and not (args.xci and args.keys):
        fail("pass --arc-dir, or --xci and --keys")

    with tempfile.TemporaryDirectory(prefix="evil-ryu-sprites-") as work:
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
        entries = {name.replace(chr(92), "/"): blob for name, blob in read_arc(arc_dir / ARCHIVE).items()}

    assets = read_assets(args.assets)
    if ASSET_PORTRAIT not in assets or ASSET_NAME not in assets:
        fail("evil_ryu_assets.bin lacks the portrait or the name")
    payload = MAGIC + struct.pack("<I", len(SPRITES) + 3)
    for pose, entry in SPRITES:
        if entry not in entries:
            fail("missing " + entry)
        width, height, indices = decode_indices(entries[entry])
        payload += struct.pack("<4I", pose, width, height, 0) + indices
    payload += indexed_record(*PORTRAIT_IMAGE, assets[ASSET_PORTRAIT], fill=True)
    payload += indexed_record(*NAME_IMAGE, assets[ASSET_NAME])
    payload += indexed_record(*HUD_NAME_IMAGE, assets[ASSET_NAME])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(payload)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
