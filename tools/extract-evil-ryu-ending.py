#!/usr/bin/env python3
"""Builds the Evil Ryu ending package from the user's own USF2 dump.

  evil_ryu_ending.bin  "SSF2VKE1" u32 count, then the ending's cuts in playing order, 512x256 A,R,G,B

Same format as violent_ken_ending.bin, so the runtime parser is shared. The output is derived from the user's game data,
belongs in the game data folder and must not be committed.
"""
import argparse
import importlib.util
import json
import struct
import sys
import tempfile
from pathlib import Path, PurePosixPath

ENDING_FOLDER = "/1/nativeNXx64/app/arc_cmn/ending"
ENDING_ARCHIVE = "ed_chr18.arc"
ENDING_ENTRY = "app\\scheduler\\ending\\chr18\\ed_chr18_%02d_ID"
ENDING_SCHEDULE = "app\\scheduler\\ending\\chr18\\ed_chr18"
CHARACTER = 18
# Frames at 60 Hz: the scene, the cut change times, the cuts in playing order, and a text line every 300 frames.
TOTAL_FRAMES = 2700
CUT_KEYS = (0, 1200, 1800, 2400)
CUT_ORDER = (1, 2, 4, 3)
TEXT_KEYS = tuple(range(0, 2700, 300))
TEXT_INDEXES = tuple(range(9))
MUSIC_REQUEST = 20
MAGIC = b"SSF2VKE1"
SIZE = (512, 256)
FIRST_ENTRY = 0x80
ENTRY_STRIDE = 0x30


def load_tool(name):
    path = Path(__file__).resolve().parent / name
    spec = importlib.util.spec_from_file_location(name.replace("-", "_").replace(".py", ""), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fail(message):
    raise SystemExit("error: " + message)


def parse_schedule(data):
    """The scheduler's parameter tracks by name: {name: (keys, values)}, plus the scene length in frames."""
    if data[:4] != b"SDL\0":
        fail("not a scheduler file")
    words = struct.unpack_from("<8I", data, 0)
    strings = words[6] + 1
    tracks = {}
    for entry in range(FIRST_ENTRY, words[6], ENTRY_STRIDE):
        kind, _, name_offset = struct.unpack_from("<3I", data, entry)
        if kind & 0xFFFF != 0x0606:
            continue
        end = data.index(b"\0", strings + name_offset)
        count = kind >> 16
        keys_at, _, values_at = struct.unpack_from("<3I", data, entry + 0x20)
        tracks[data[strings + name_offset:end].decode("ascii")] = (
            struct.unpack_from("<%dI" % count, data, keys_at), struct.unpack_from("<%dI" % count, data, values_at))
    return tracks, words[3]


def ending_cuts(assets, archive):
    from PIL import Image

    entries = assets.read_arc(archive)
    tracks, frames = parse_schedule(entries[ENDING_SCHEDULE])
    expected = {"ChrID": ((0,), (CHARACTER,)), "CutNo": (CUT_KEYS, CUT_ORDER), "TextIndex": (TEXT_KEYS, TEXT_INDEXES),
                "BgmReqNo": ((0,), (MUSIC_REQUEST,))}
    for name, track in expected.items():
        if tracks.get(name) != track:
            fail("unexpected ending schedule: " + name)
    if frames != TOTAL_FRAMES:
        fail("unexpected ending length")
    blob = MAGIC + struct.pack("<I", len(CUT_ORDER))
    images = []
    for cut in CUT_ORDER:
        art = assets.decode_tex(entries[ENDING_ENTRY % cut]).convert("RGBA").resize(SIZE, Image.LANCZOS)
        rgba = art.tobytes()
        argb = bytearray(len(rgba))
        argb[0::4], argb[1::4], argb[2::4], argb[3::4] = b"\xff" * (len(rgba) // 4), rgba[0::4], rgba[1::4], rgba[2::4]
        blob += bytes(argb)
        images.append(art)
    return blob, images


def main():
    root = Path(__file__).resolve().parent.parent
    references = root / "game/references"
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--xci", type=Path,
                        default=references / "Ultra Street Fighter II - The Final Challengers (USA) (En,Fr,De,Es,It).xci")
    parser.add_argument("--keys", type=Path, default=references / "prod.keys")
    parser.add_argument("--kit", type=Path, default=references / "usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    parser.add_argument("--ending-dir", type=Path, help="folder already holding ed_chr18.arc")
    parser.add_argument("--out-dir", type=Path, default=root / "game/extracted")
    parser.add_argument("--preview-dir", type=Path, default=root / "out/evil-ryu-analysis/ending")
    args = parser.parse_args()

    assets = load_tool("extract-evil-ryu-assets.py")

    def extract(ending_dir):
        archive = ending_dir / ENDING_ARCHIVE
        if not archive.is_file():
            fail("missing " + str(archive))
        blob, cuts = ending_cuts(assets, archive)
        args.out_dir.mkdir(parents=True, exist_ok=True)
        (args.out_dir / "evil_ryu_ending.bin").write_bytes(blob)
        args.preview_dir.mkdir(parents=True, exist_ok=True)
        for index, cut in enumerate(cuts):
            cut.save(args.preview_dir / ("ending_cut_%d.png" % (index + 1)))

    if args.ending_dir:
        extract(args.ending_dir)
    else:
        sys.path.insert(0, str(args.kit))
        import switch_source

        pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
        nstool = args.nstool or assets.find_nstool()
        with tempfile.TemporaryDirectory(prefix="evil-ryu-ending-") as work:
            with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
                switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(ENDING_FOLDER), Path(work) / "ending")
            extract(Path(work) / "ending")
    print("wrote the Evil Ryu ending package to", args.out_dir)


if __name__ == "__main__":
    main()
