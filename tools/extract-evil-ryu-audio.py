#!/usr/bin/env python3
"""Builds evil_ryu_audio.bin; see extract-ultra-character-audio.py."""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location("extract_ultra_character_audio",
                                              Path(__file__).resolve().parent / "extract-ultra-character-audio.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

if __name__ == "__main__":
    tool.main("evil_ryu")
