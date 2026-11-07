#!/usr/bin/env python3
"""Covers the CPS-2 backport analysis tools with synthetic data; no game data is read."""
import hashlib
import importlib.util
import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), ROOT / "tools" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


analyze = load("analyze-usf2-backport")


class PatchFormat(unittest.TestCase):
    def payload(self, delta, key):
        compressed = zlib.compress(delta, 9)
        identity = "us:" + hashlib.sha256(compressed).hexdigest()
        wrapped = analyze.unmask(compressed, key, identity)
        return wrapped, {"identity": identity, "sha256": hashlib.sha256(wrapped).hexdigest(),
                         "decoded_sha256": hashlib.sha256(compressed).hexdigest()}

    def test_round_trip(self):
        key = analyze.source_key(b"resource" * 8)
        wrapped, record = self.payload(b"\0\0\x12\0", key)
        self.assertEqual(analyze.decode_delta(wrapped, record, key), b"\0\0\x12\0")

    def test_wrong_dump_is_rejected(self):
        wrapped, record = self.payload(b"\x01", analyze.source_key(b"a" * 32))
        with self.assertRaises(ValueError):
            analyze.decode_delta(wrapped, record, analyze.source_key(b"b" * 32))

    def test_tampered_payload_is_rejected(self):
        key = analyze.source_key(b"a" * 32)
        wrapped, record = self.payload(b"\x01", key)
        with self.assertRaises(ValueError):
            analyze.decode_delta(bytes([wrapped[0] ^ 1]) + wrapped[1:], record, key)

    def test_extents_merge_small_gaps(self):
        delta = bytearray(100)
        delta[2] = delta[10] = delta[60] = 1
        self.assertEqual(analyze.changed_extents(bytes(delta), gap=16), [(2, 11), (60, 61)])

    def test_classification(self):
        self.assertEqual(analyze.classify("usf2.10"), "program")
        self.assertEqual(analyze.classify("sfxj.03d"), "program")
        self.assertEqual(analyze.classify("usf2.29m"), "graphics")
        self.assertEqual(analyze.classify("sfx.11m"), "audio")
        self.assertEqual(analyze.classify("sfx.01"), "z80")
        self.assertEqual(analyze.classify("ssf2xj.key"), "key")


class Cps2Graphics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.cps2 = load("extract-evil-ryu-cps2")
        except ImportError as error:
            raise unittest.SkipTest(str(error))

    def test_interleave_is_word_wise(self):
        import numpy as np

        banks = [np.array([i, i + 10], dtype=np.uint8) for i in range(1, 5)]
        self.assertEqual(self.cps2.interleave(banks).tolist(), [1, 11, 2, 12, 3, 13, 4, 14])

    def test_unshuffle_matches_mame(self):
        import numpy as np

        rows = np.arange(8, dtype=np.uint64).reshape(-1, 1).copy()
        self.cps2.unshuffle(rows)
        self.assertEqual(rows.ravel().tolist(), [0, 2, 4, 6, 1, 3, 5, 7])

    def test_tile_pen_decoding(self):
        import numpy as np

        tile = np.zeros(128, dtype=np.uint8)
        tile[0] = 0x80  # plane 0, leftmost pixel of the first row
        tile[3] = 0x80  # plane 3
        tile[4] = 0x01  # plane 0, last pixel of the left half
        pixels = self.cps2.decode_tile(tile)
        self.assertEqual(int(pixels[0, 0]), 9)
        self.assertEqual(int(pixels[0, 15]), 1)


if __name__ == "__main__":
    unittest.main()
