#!/usr/bin/env python3
"""Builds evil_ryu_hud_icon.bin (the Evil Ryu fight HUD portrait) from the user's own USF2 and HD Remix data.

gui_battle.arc holds only charaface layouts, no face texture, so the face comes from gui_sel_face chara18_01_ID, mirrored
to the left facing of the native HUD_Icon textures and cropped like them. The silver frame is from native HUD_IconRyu.

Output: "SSF2ERI1" then 128x128 pixels, 4 bytes each in the texture's own A,R,G,B order, top-down rows. It is derived
from the user's game data and must not be committed or redistributed.
"""
import argparse
import importlib.util
import struct
import zlib
from pathlib import Path

MAGIC = b"SSF2ERI1"
ICON_SIZE = 128
ICON_ENTRY = "textures/user_interface/In_game/GameScreen/HUD_IconRyu.texture.x360"
ICON_PIXELS_AT = 45
FACE_ARCHIVE = "gui_sel_face.arc"
FACE_ENTRY = "app\\ui\\battle\\face\\chara18_01_ID"
FACE_CROP = (22, 22, 494, 494)
WINDOW = (6, 6, 122, 122)
BACKDROP = (0, 0, 0, 170)


def fail(message):
    raise SystemExit("error: " + message)


def read_fpg(path, name):
    data = Path(path).read_bytes()
    count = struct.unpack_from("<I", data, 4)[0]
    wanted = zlib.crc32(name.replace("\\", "/").upper().encode("latin-1"))
    for i in range(count):
        key, offset, size, _ = struct.unpack_from("<IIII", data, 0x800 + i * 16)
        if key == wanted:
            raw = data[offset:offset + size]
            try:
                return zlib.decompress(raw)
            except zlib.error:
                return raw
    fail("missing " + name)


def load_assets_tool():
    path = Path(__file__).resolve().parent / "extract-evil-ryu-assets.py"
    spec = importlib.util.spec_from_file_location("evil_ryu_assets_tool", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def native_frame(texture):
    from PIL import Image

    pixels = texture[ICON_PIXELS_AT:ICON_PIXELS_AT + ICON_SIZE * ICON_SIZE * 4]
    rgba = bytearray(len(pixels))
    rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4] = pixels[1::4], pixels[2::4], pixels[3::4], pixels[0::4]
    return Image.frombytes("RGBA", (ICON_SIZE, ICON_SIZE), bytes(rgba))


def build_icon(frame, face):
    from PIL import Image, ImageOps

    left, top, right, bottom = WINDOW
    size = (right - left, bottom - top)
    art = ImageOps.mirror(face).crop((face.width - FACE_CROP[2], FACE_CROP[1], face.width - FACE_CROP[0], FACE_CROP[3]))
    art = art.resize(size, Image.LANCZOS)
    window = Image.new("RGBA", size, BACKDROP)
    window.alpha_composite(art)
    icon = frame.copy()
    icon.paste(window, (left, top))
    return icon


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gui", type=Path, default=root / "out/evil-ryu-analysis/usf2/gui",
                        help="folder holding the USF2 gui archives")
    parser.add_argument("--fpg", type=Path, default=root / "game/extracted/flog_x360.fpg")
    parser.add_argument("--out", type=Path, default=root / "game/extracted/evil_ryu_hud_icon.bin")
    parser.add_argument("--preview", type=Path, help="optional PNG preview")
    args = parser.parse_args()

    tool = load_assets_tool()
    faces = tool.read_arc(args.gui / FACE_ARCHIVE)
    if FACE_ENTRY not in faces:
        fail("missing " + FACE_ENTRY)
    icon = build_icon(native_frame(read_fpg(args.fpg, ICON_ENTRY)), tool.decode_tex(faces[FACE_ENTRY]))
    rgba = icon.tobytes()
    argb = bytearray(len(rgba))
    argb[0::4], argb[1::4], argb[2::4], argb[3::4] = rgba[3::4], rgba[0::4], rgba[1::4], rgba[2::4]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(MAGIC + bytes(argb))
    if args.preview:
        from PIL import Image

        args.preview.parent.mkdir(parents=True, exist_ok=True)
        icon.resize((256, 256), Image.NEAREST).save(args.preview)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
