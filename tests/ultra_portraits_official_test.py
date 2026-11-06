#!/usr/bin/env python3
"""Covers the Ultra portrait extractor; the checks that need the user's USF2 files skip without them."""
import hashlib
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None

ROOT = Path(__file__).resolve().parent.parent
GUI = ROOT / "out" / "ultra-portraits" / "gui"
INSTALLED = ROOT / "assets" / "characters" / "ultra_portraits.bin"

spec = importlib.util.spec_from_file_location("extract_ultra_portraits", ROOT / "tools" / "extract-ultra-portraits.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

OFFICIAL = {
    "tex_face18": (r"app\ui\battle\face\chara18_01_ID", (393, 52, 51, 460),
                   "a5dac48ce01147a01d846876f313a7de0be128baf45004bf81acd00d956a3c25"),
    "tex_face19": (r"app\ui\battle\face\chara19_01_ID", (439, 52, 97, 460),
                   "1dc60ce97d13216614c9d9c244131719a2405a52e3407ada311f57832d49e26a"),
}
# Capcom gives each new fighter the rect of the fighter it derives from.
SIBLINGS = {"tex_face18": "tex_face01", "tex_face19": "tex_face05"}


def layout(cell_rect, keys):
    """A minimal Character Select layout with one cell, keyed by (frame, rect)."""
    strings = b"\0".join([b"tex_face18", b"BlendState", b"mTextureRect", b"Texture", b"tex\\path_ID"]) + b"\0"
    offset = {"cell": 0, "blend": 11, "rect": 22, "texture": 35, "path": 43}
    object_section, param_section, keyed_section, texture_section = 0x200, 0x300, 0x400, 0x500
    key_section, index_section, value_section, string_section = 0x600, 0x700, 0x800, 0xA00
    sections = [0] * 36
    sections[3], sections[4] = object_section, object_section + 0x38
    sections[5], sections[6], sections[7] = param_section, keyed_section, keyed_section + 0x28
    sections[18], sections[19] = texture_section, texture_section + 0x40
    sections[25], sections[26], sections[28], sections[29] = string_section, key_section, index_section, value_section
    data = bytearray(0xC00)
    data[:4] = b"GUI\0"
    struct.pack_into("<36Q", data, 0xA0, *sections)
    struct.pack_into("<I", data, object_section, 27)
    struct.pack_into("<I", data, object_section + 0x10, offset["cell"])
    struct.pack_into("<I", data, object_section + 0x20, 0)
    for index, (name, value) in enumerate((("blend", 1), ("rect", 0), ("texture", 0))):
        struct.pack_into("<II", data, param_section + index * 0x20 + 0x10, offset[name], 0)
        struct.pack_into("<I", data, param_section + index * 0x20 + 0x18, value)
    struct.pack_into("<I", data, keyed_section + 8, (len(keys) << 8) | 4)
    struct.pack_into("<II", data, keyed_section + 0xC, 27, offset["rect"])
    struct.pack_into("<I", data, keyed_section + 0x18, 0)
    struct.pack_into("<I", data, keyed_section + 0x20, 0x10)
    struct.pack_into("<4f", data, value_section, *cell_rect)
    for index, (frame, rect) in enumerate(keys):
        struct.pack_into("<I", data, key_section + index * 16, 0x3000000 | frame)
        struct.pack_into("<4f", data, value_section + 0x10 + index * 16, *rect)
    struct.pack_into("<I", data, index_section, 9)
    struct.pack_into("<I", data, texture_section, 9)
    struct.pack_into("<I", data, texture_section + 0x30, offset["path"])
    data[string_section:string_section + len(strings)] = strings
    return bytes(data)


class LayoutParsing(unittest.TestCase):
    def test_last_key_of_frame_zero_wins(self):
        keyed = layout((1, 2, 3, 4), [(0, (5, 6, 7, 8)), (0, (9, 10, 11, 12))])
        self.assertEqual(tool.select_cell(keyed, "tex_face18"), ("tex\\path_ID", (9, 10, 11, 12)))

    def test_later_frames_do_not_replace_the_cell(self):
        keyed = layout((1, 2, 3, 4), [(0, (5, 6, 7, 8)), (1, (9, 10, 11, 12))])
        self.assertEqual(tool.select_cell(keyed, "tex_face18")[1], (5, 6, 7, 8))

    def test_unkeyed_cell_keeps_its_initial_rect(self):
        self.assertEqual(tool.select_cell(layout((1, 2, 3, 4), []), "tex_face18")[1], (1, 2, 3, 4))

    def test_bad_inputs_fail(self):
        with self.assertRaises(SystemExit):
            tool.select_cell(b"XXXX" + bytes(0x200), "tex_face18")
        with self.assertRaises(SystemExit):
            tool.select_cell(layout((1, 2, 3, 4), []), "tex_face99")


class Sources(unittest.TestCase):
    def test_no_framing_numbers_live_in_the_tool(self):
        source = (ROOT / "tools" / "extract-ultra-portraits.py").read_text()
        self.assertNotIn("charaicon", source)
        for _, rect, _ in OFFICIAL.values():
            self.assertNotIn(", ".join(str(part) for part in rect), source)


@unittest.skipIf(Image is None, "Pillow is not installed")
class Conversion(unittest.TestCase):
    def gradient(self, width=512, height=512):
        face = Image.new("RGBA", (width, height))
        face.putdata([(x % 256, y % 256, 1, 255) for y in range(height) for x in range(width)])
        return face

    def test_reversed_rect_mirrors_the_complete_extent(self):
        face = self.gradient()
        cell = tool.official_cell(face, (393, 52, 51, 460))
        self.assertEqual(cell.size, (342, 408))
        self.assertEqual(cell.getpixel((0, 0))[:2], (392 % 256, 52))
        self.assertEqual(cell.getpixel((341, 407))[:2], (51, 459 % 256))

    def test_forward_rect_keeps_the_direction(self):
        cell = tool.official_cell(self.gradient(), (100, 94, 442, 502))
        self.assertEqual(cell.getpixel((0, 0))[:2], (100, 94))

    def test_rect_outside_the_texture_fails(self):
        with self.assertRaises(SystemExit):
            tool.official_cell(self.gradient(), (492, 292, 606, 428))

    def test_fit_preserves_the_on_screen_aspect(self):
        width, height = tool.fitted_size(342, 408)
        self.assertEqual((width, height), (112, 116))
        shown = (width * tool.CELL_WIDTH) / (height * tool.CELL_HEIGHT)
        self.assertAlmostEqual(shown, 342 / 408, places=2)

    def test_art_stays_inside_the_frame_band_uncropped(self):
        art = tool.build_art(Image.new("RGBA", (342, 408), (200, 10, 20, 255)))
        self.assertEqual(art.size, (tool.SIZE, tool.SIZE))
        solid = {(x, y) for y in range(tool.SIZE) for x in range(tool.SIZE) if art.getpixel((x, y))[0] > 0}
        columns, rows = {x for x, _ in solid}, {y for _, y in solid}
        self.assertEqual((min(columns), max(columns)), (8, 119))
        self.assertEqual((min(rows), max(rows)), (6, 121))


@unittest.skipIf(Image is None or not GUI.is_dir(), "needs Pillow and out/ultra-portraits/gui")
class OfficialAssets(unittest.TestCase):
    def layout(self):
        return tool.read_arc(GUI / tool.SELECT_ARCHIVE)[tool.SELECT_LAYOUT]

    def test_cells_bind_the_expected_textures_and_rects(self):
        for name, (texture, rect, _) in OFFICIAL.items():
            self.assertEqual(tool.select_cell(self.layout(), name), (texture, rect))

    def test_cells_share_their_siblings_rects(self):
        for name, sibling in SIBLINGS.items():
            self.assertEqual(tool.select_cell(self.layout(), name)[1], tool.select_cell(self.layout(), sibling)[1])

    def test_source_texture_format(self):
        faces = tool.read_arc(GUI / tool.FACE_ARCHIVE)
        for texture, _, _ in OFFICIAL.values():
            tex = faces[texture]
            fields = struct.unpack_from("<I", tex, 8)[0]
            self.assertEqual(((fields >> 6) & 0x1FFF, (fields >> 19) & 0x1FFF, fields & 0x3F), (512, 512, 1))
            self.assertEqual(len(tex), 24 + 512 * 512)

    def test_decoded_cells_match_the_official_pixels(self):
        for name, (_, _, digest) in OFFICIAL.items():
            cell = tool.load_cell(GUI, name)
            self.assertEqual((cell.size, cell.mode), ((342, 408), "RGBA"))
            low, high = cell.getchannel("A").getextrema()
            self.assertEqual((low, high), (0, 255))
            self.assertEqual(hashlib.sha256(cell.tobytes()).hexdigest(), digest)

    def test_installed_package_holds_exactly_the_official_conversion(self):
        built = tool.build_records(GUI)
        self.assertEqual(sorted(built), [0, 1])
        if not INSTALLED.is_file():
            self.skipTest("no installed ultra_portraits.bin")
        file = INSTALLED.read_bytes()
        self.assertEqual(file[:8], tool.MAGIC)
        self.assertEqual(struct.unpack_from("<I", file, 8)[0], 2)
        at = 12
        for portrait_id, art in built.items():
            self.assertEqual(struct.unpack_from("<3I", file, at), (portrait_id, tool.SIZE, tool.SIZE))
            at += 12
            self.assertEqual(file[at:at + tool.SIZE * tool.SIZE * 4], art.tobytes())
            at += tool.SIZE * tool.SIZE * 4
        self.assertEqual(at, len(file))


if __name__ == "__main__":
    unittest.main()
