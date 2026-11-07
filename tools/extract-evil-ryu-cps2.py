#!/usr/bin/env python3
"""Renders the new graphics of the USF2 CPS-2 backport from the members tools/analyze-usf2-backport.py recovered.

Everything this writes is derived from the user's game data and must not be committed.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MEMBERS = ROOT / "out/evil-ryu-analysis/cps2/members"
DEFAULT_OUT = ROOT / "out/evil-ryu-analysis/cps2/previews"
BANKS = ("usf2.29m", "usf2.31m", "usf2.33m", "usf2.35m")
BANK_BYTES = 0x200000
TILE_BYTES = 128
PAGE_TILES_WIDE = 64


def interleave(banks):
    words = len(banks[0]) // 2
    out = np.zeros(words * 8, dtype=np.uint8)
    for index, bank in enumerate(banks):
        out[index * 2::8] = bank[0::2]
        out[index * 2 + 1::8] = bank[1::2]
    return out


def unshuffle(rows):
    """CPS-2 stores each 2 MiB bank in a recursive block order; MAME undoes it the same way."""
    if len(rows) <= 2:
        return
    half = len(rows) // 2
    unshuffle(rows[:half])
    unshuffle(rows[half:])
    quarter = half // 2
    swap = rows[quarter:half].copy()
    rows[quarter:half] = rows[half:half + quarter]
    rows[half:half + quarter] = swap


def load_tiles(members, profile):
    banks = [np.frombuffer((members / (profile + "_" + name)).read_bytes(), dtype=np.uint8) for name in BANKS]
    data = interleave(banks)
    rows = data.reshape(-1, 8).copy()
    for start in range(0, len(rows), BANK_BYTES // 8):
        unshuffle(rows[start:start + BANK_BYTES // 8])
    return rows.reshape(-1, TILE_BYTES)


def decode_tile(tile):
    bits = np.unpackbits(tile.reshape(16, 8), axis=1)
    pixels = np.zeros((16, 16), dtype=np.uint8)
    for x in range(8):
        for plane in range(4):
            pixels[:, x] |= bits[:, plane * 8 + x] << plane
            pixels[:, 8 + x] |= bits[:, 32 + plane * 8 + x] << plane
    return pixels


def render_page(tiles, start, rows, wide=PAGE_TILES_WIDE):
    image = np.full((rows * 16, wide * 16), 15, dtype=np.uint8)
    for index in range(min(rows * wide, len(tiles) - start)):
        y, x = index // wide, index % wide
        image[y * 16:y * 16 + 16, x * 16:x * 16 + 16] = decode_tile(tiles[start + index])
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--members", type=Path, default=DEFAULT_MEMBERS)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--profile", default="usf2t8")
    parser.add_argument("--start", type=lambda v: int(v, 0), default=0)
    parser.add_argument("--rows", type=int, default=340)
    parser.add_argument("--scale", type=int, default=4)
    parser.add_argument("--name", default="atlas")
    args = parser.parse_args()

    tiles = load_tiles(args.members, args.profile)
    args.out.mkdir(parents=True, exist_ok=True)
    page = render_page(tiles, args.start, args.rows)
    image = Image.fromarray((15 - page) * 17)
    if args.scale > 1:
        image = image.resize((image.width // args.scale, image.height // args.scale), Image.BILINEAR)
    image.save(args.out / (args.name + ".png"))
    print("wrote", args.out / (args.name + ".png"), image.size)


if __name__ == "__main__":
    main()
