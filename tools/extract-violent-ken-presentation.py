#!/usr/bin/env python3
"""Builds the Violent Ken art and palette packages from the user's own USF2 dump and HD Remix files.

Reuses the Evil Ryu tools' decoders and their file formats, so the runtime parsers stay shared:
  violent_ken_assets.bin      "SSF2ERA1" large portrait and name graphic, RGBA
  violent_ken_sprites.bin     "SSF2ERS2" the dash pose (pose 280), large portrait, Select and HUD names, beaten portrait
  violent_ken_ending.bin      "SSF2VKE1" u32 count, then the ending's cuts in playing order, 512x256 A,R,G,B
  violent_ken_hd_palette.bin  "SSF2ERH1" palettes for the HD Ken sheets
  violent_ken_palette.bin     "SSF2ERP1" Ken and Violent Ken pens for the classic sprites
  violent_ken_hud_icon.bin    "SSF2ERI1" the fight HUD portrait

The output is derived from the user's game data, belongs in the game data folder and must not be committed.
"""
import argparse
import importlib.util
import json
import struct
import sys
import tempfile
from pathlib import Path, PurePosixPath

ROOT_FOLDER = "/1/nativeNXx64/app/arc_cmn"
FACE_ENTRY = "app\\ui\\battle\\face\\chara19_01_ID"
LOSE_ARCHIVE = "chr19_01.arc"
LOSE_ENTRY = "app\\ui\\battle\\loseface\\chara19_lose_01_ID"
ASSET_LOSE = 4
ENDING_ARCHIVE = "ed_chr19.arc"
ENDING_ENTRY = "app\\scheduler\\ending\\chr19\\ed_chr19_%02d_ID"
ENDING_SCHEDULE = "app\\scheduler\\ending\\chr19\\ed_chr19"
# mCutNo of the scheduler: keys at 0x1B4, values at 0x1D0.
ENDING_KEYS = (0, 1200, 2100, 2400)
ENDING_CUTS = (1, 2, 4, 3)
ENDING_MAGIC = b"SSF2VKE1"
ENDING_SIZE = (512, 256)
NAME_ENTRY = "app\\ui\\battle\\characterselect\\name02_ID"
SPRITE_ENTRY = "app/chr/chr19/n/tex/chr19_200_00"
NAME_PROBE_Y = 640
DEFAULT_COLOUR = 1
MIRROR_COLOUR = 7
COLOURS = 10


def load_tool(name):
    path = Path(__file__).resolve().parent / name
    spec = importlib.util.spec_from_file_location(name.replace("-", "_").replace(".py", ""), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fail(message):
    raise SystemExit("error: " + message)


def shared_palette_records(sprites, images):
    """Indexed records over one palette: the guest's palette row names one texture for both portraits."""
    from PIL import Image

    canvases = []
    for record, width, height, source in images:
        canvases.append((record, sprites.fit_canvas(Image.frombytes("RGBA", source[:2], source[2]), width, height, True)))
    height = max(c.height for _, c in canvases)
    sheet = Image.new("RGB", (sum(c.width for _, c in canvases), height))
    x = 0
    for _, canvas in canvases:
        sheet.paste(canvas.convert("RGB"), (x, 0))
        x += canvas.width
    palette = sheet.quantize(colors=255, method=Image.MEDIANCUT)
    colours = palette.getpalette()[:255 * 3]
    colours += [0] * (255 * 3 - len(colours))
    table = bytearray(sprites.COLOUR_KEY)
    for i in range(255):
        red, green, blue = colours[i * 3:i * 3 + 3]
        table += bytes((0xFF, blue, green, red))
    records = {}
    for record, canvas in canvases:
        mapped = canvas.convert("RGB").quantize(palette=palette, dither=Image.FLOYDSTEINBERG)
        alpha = canvas.getchannel("A").point(lambda v: 255 if v >= 128 else 0)
        pixels = bytes(0 if a == 0 else p + 1 for p, a in zip(mapped.tobytes(), alpha.tobytes()))
        records[record] = struct.pack("<4I", record, canvas.width, canvas.height, 1) + bytes(table) + pixels
    return records


def ending_cuts(assets, archive):
    from PIL import Image

    entries = assets.read_arc(archive)
    schedule = entries[ENDING_SCHEDULE]
    keys = struct.unpack_from("<4I", schedule, 0x1B4)
    cuts = struct.unpack_from("<4I", schedule, 0x1D0)
    if keys != ENDING_KEYS or cuts != ENDING_CUTS:
        fail("unexpected ending schedule")
    blob = ENDING_MAGIC + struct.pack("<I", len(cuts))
    images = []
    for cut in cuts:
        art = assets.decode_tex(entries[ENDING_ENTRY % cut]).convert("RGBA").resize(ENDING_SIZE, Image.LANCZOS)
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
    parser.add_argument("--fpg", type=Path, default=root / "game/extracted/flog_x360.fpg")
    parser.add_argument("--arc-root", type=Path, help="folder already holding gui/, btl/, preview/, face/ and ending/")
    parser.add_argument("--out-dir", type=Path, default=root / "game/extracted")
    parser.add_argument("--preview-dir", type=Path, default=root / "out/violent-ken-analysis/presentation")
    args = parser.parse_args()

    assets = load_tool("extract-evil-ryu-assets.py")
    sprites = load_tool("extract-evil-ryu-sprites.py")
    hd = load_tool("extract-evil-ryu-hd-palette.py")
    classic = load_tool("extract-evil-ryu-palette.py")
    icon = load_tool("extract-evil-ryu-hud-icon.py")

    def extract(arc_root):
        gui, btl, preview = arc_root / "gui", arc_root / "btl", arc_root / "preview"
        lose_faces = assets.read_arc(arc_root / "face" / LOSE_ARCHIVE)
        if LOSE_ENTRY not in lose_faces:
            fail("missing " + LOSE_ENTRY)
        beaten = assets.build_portrait(assets.decode_tex(lose_faces[LOSE_ENTRY]))
        faces = assets.read_arc(gui / assets.FACE_ARCHIVE)
        names = assets.read_arc(gui / assets.NAME_ARCHIVE)
        if FACE_ENTRY not in faces or NAME_ENTRY not in names:
            fail("missing Violent Ken face or name entry")
        assets.NAME_PROBE_Y = NAME_PROBE_Y
        face = assets.decode_tex(faces[FACE_ENTRY])
        portrait = assets.build_portrait(face)
        name = assets.build_name(assets.decode_tex(names[NAME_ENTRY]))

        args.out_dir.mkdir(parents=True, exist_ok=True)
        records = [(sprites.ASSET_PORTRAIT, portrait), (sprites.ASSET_NAME, name), (ASSET_LOSE, beaten)]
        packed = [struct.pack("<3I", i, m.width, m.height) + m.tobytes() for i, m in records]
        asset_path = args.out_dir / "violent_ken_assets.bin"
        asset_path.write_bytes(assets.MAGIC + struct.pack("<I", len(packed)) + b"".join(packed))

        entries = {n.replace(chr(92), "/"): blob for n, blob in sprites.read_arc(btl / "n_chr19.arc").items()}
        if SPRITE_ENTRY not in entries:
            fail("missing " + SPRITE_ENTRY)
        width, height, indices = sprites.decode_indices(entries[SPRITE_ENTRY])
        loaded = sprites.read_assets(asset_path)
        payload = sprites.MAGIC + struct.pack("<I", 6)
        for pose in (224, 225):
            payload += struct.pack("<4I", pose, width, height, 0) + indices
        portraits = shared_palette_records(sprites, [(*sprites.PORTRAIT_IMAGE, loaded[sprites.ASSET_PORTRAIT]),
                                                     (ASSET_LOSE, *sprites.PORTRAIT_IMAGE[1:], loaded[ASSET_LOSE])])
        payload += portraits[sprites.PORTRAIT_IMAGE[0]]
        payload += sprites.indexed_record(*sprites.NAME_IMAGE, loaded[sprites.ASSET_NAME])
        payload += sprites.indexed_record(*sprites.HUD_NAME_IMAGE, loaded[sprites.ASSET_NAME])
        payload += portraits[ASSET_LOSE]
        (args.out_dir / "violent_ken_sprites.bin").write_bytes(payload)

        battle = hd.read_arc(btl / "n_chr19.arc")
        hd.ARCHIVE = "n_chr19.arc"
        palettes = []
        for colour in [DEFAULT_COLOUR] * COLOURS + [MIRROR_COLOUR]:
            palette = battle.get("app/chr/chr19/n/pal/chr19_pal%02d" % colour)
            if palette is None:
                fail("missing palette %d" % colour)
            battle["app/chr/chr18/n/pal/chr18_pal%02d" % colour] = palette
            palettes.append(hd.guest_palette(battle, colour))
        (args.out_dir / "violent_ken_hd_palette.bin").write_bytes(hd.MAGIC + struct.pack("<I", len(palettes)) +
                                                                 b"".join(palettes))

        ken = classic.palettes(preview / "prev_c_chr05.arc", 5)
        violent = classic.palettes(preview / "prev_c_chr19.arc", 19)
        pens = classic.MAGIC + struct.pack("<I", classic.COLOURS)
        for words in ken:
            pens += struct.pack("<16H", *words) + struct.pack("<16H", *violent[classic.DARK_COLOUR])
        (args.out_dir / "violent_ken_palette.bin").write_bytes(pens)

        built = icon.build_icon(icon.native_frame(icon.read_fpg(args.fpg, icon.ICON_ENTRY)), face)
        rgba = built.tobytes()
        argb = bytearray(len(rgba))
        argb[0::4], argb[1::4], argb[2::4], argb[3::4] = rgba[3::4], rgba[0::4], rgba[1::4], rgba[2::4]
        (args.out_dir / "violent_ken_hud_icon.bin").write_bytes(icon.MAGIC + bytes(argb))

        ending, cuts = ending_cuts(assets, arc_root / "ending" / ENDING_ARCHIVE)
        (args.out_dir / "violent_ken_ending.bin").write_bytes(ending)

        args.preview_dir.mkdir(parents=True, exist_ok=True)
        portrait.save(args.preview_dir / "large_portrait.png")
        beaten.save(args.preview_dir / "lose_portrait.png")
        for index, cut in enumerate(cuts):
            cut.save(args.preview_dir / ("ending_cut_%d.png" % (index + 1)))
        name.save(args.preview_dir / "name.png")
        built.save(args.preview_dir / "hud_icon.png")

    if args.arc_root:
        extract(args.arc_root)
    else:
        sys.path.insert(0, str(args.kit))
        import switch_source

        pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
        nstool = args.nstool or assets.find_nstool()
        with tempfile.TemporaryDirectory(prefix="violent-ken-art-") as work:
            with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
                for folder in ("gui", "btl", "preview", "face", "ending"):
                    switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(ROOT_FOLDER + "/" + folder),
                                          Path(work) / folder)
            extract(Path(work))
    print("wrote the Violent Ken packages to", args.out_dir)


if __name__ == "__main__":
    main()
