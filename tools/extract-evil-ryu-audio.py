#!/usr/bin/env python3
"""Builds evil_ryu_audio.bin (Evil Ryu voice and announcer clips) from the user's own USF2 dump.

The output is derived from the user's game data and belongs next to the HD Remix game files. It must not be
committed or redistributed. Nothing is played; the tool only reads the dump and writes the package.

USF2 wave entries are Nintendo DSP ADPCM: 0x08 u32 data bytes, 0x0C f32 seconds, 0x18 16 s16 coefficients,
0x60 u32 samples, 0x64 u32 rate, 0x68 u32 loop flag, 0x70 u32 last sample, 0x78 8-byte frames of 14 samples.

Package (little endian): "SSF2ERV1", u32 count, then per clip u32 id, u32 rate, u32 samples, s16 mono samples.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

MAGIC = b"SSF2ERV1"
SOUND_FOLDER = "/1/nativeNXx64/app/arc_cmn/snd"
WAVE_HEADER = bytes.fromhex("0200000001000001")
DATA_OFFSET = 0x78
FRAME_BYTES = 8
FRAME_SAMPLES = 14
SILENCE_LEVEL = 200
VOICE_PEAK = 29000
MAX_VOICE_GAIN = 2.0

VOICE_BANK = "chr18_se_ngs.arc"
COMMON_BANK = "common_se_ngs.arc"
VOICE_FOLDER = "app\\sound\\se\\ngs\\chr18\\wav\\"
COMMON_FOLDER = "app\\sound\\se\\ngs\\com\\wav\\voice\\ngs\\"

# (package id, name, archive, entry, level the clip like a character voice, drop trailing silence)
CLIPS = (
    (1, "hadoken", VOICE_BANK, VOICE_FOLDER + "se_ng_chr18_v_28", True, False),
    (2, "shoryuken", VOICE_BANK, VOICE_FOLDER + "se_ng_chr18_v_29", True, False),
    (3, "tatsumaki", VOICE_BANK, VOICE_FOLDER + "se_ng_chr18_v_30", True, False),
    (4, "ko", VOICE_BANK, VOICE_FOLDER + "se_ng_chr18_v_69", True, False),
    (5, "shun_goku_satsu", VOICE_BANK, VOICE_FOLDER + "se_ng_chr18_v_senku", True, False),
    (6, "announcer_name", COMMON_BANK, COMMON_FOLDER + "se_v_com_evil_ryu_ng_jp", False, True),
)

# HD Remix sound ids of the Ryu events each USF2 request replaces. KO is one id; the fight work byte names the loser.
HD_EVENTS = {
    0: ("KO", "0x64 (100), loser in fight work +1356"),
    1: ("Hadoken", "P1 0x60 (96), P2 0x136 (310)"),
    2: ("Shoryuken", "P1 0x61 (97), P2 0x137 (311)"),
    3: ("Tatsumaki", "P1 0x62 (98), P2 0x138 (312)"),
    12: ("Shun Goku Satsu", "host move start, carried on the P1 or P2 Hadoken id"),
}


def fail(message):
    raise SystemExit("error: " + message)


def find_nstool():
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
    entries = []
    for i in range(count):
        at = 8 + i * 0x50
        name = data[at:at + 64].split(b"\0")[0].decode("latin-1")
        _, packed, flags, offset = struct.unpack_from("<IIII", data, at + 64)
        size = flags & 0x1FFFFFFF
        blob = data[offset:offset + packed]
        entries.append((name, zlib.decompress(blob) if packed != size else blob))
    return entries


def find_entry(entries, name):
    for entry, raw in entries:
        if entry == name and raw[:8] == WAVE_HEADER:
            return raw
    fail("missing " + name)


def decode_dsp(raw):
    coefs = struct.unpack_from("<16h", raw, 0x18)
    samples, rate, loop = struct.unpack_from("<III", raw, 0x60)
    data_bytes = struct.unpack_from("<I", raw, 8)[0]
    if loop != 0 or samples == 0:
        fail("unexpected wave header")
    tail = samples % FRAME_SAMPLES
    if samples // FRAME_SAMPLES * FRAME_BYTES + (1 + (tail + 1) // 2 if tail else 0) != data_bytes:
        fail("wave size does not match its sample count")
    pcm = []
    hist1 = hist2 = 0
    at = DATA_OFFSET
    while len(pcm) < samples:
        header = raw[at]
        predictor = header >> 4
        if predictor > 7:
            fail("invalid DSP predictor")
        scale = 1 << (header & 15)
        coef1, coef2 = coefs[predictor * 2], coefs[predictor * 2 + 1]
        for i in range(min(FRAME_SAMPLES, samples - len(pcm))):
            byte = raw[at + 1 + i // 2]
            nibble = byte >> 4 if i % 2 == 0 else byte & 15
            if nibble >= 8:
                nibble -= 16
            value = (((nibble * scale) << 11) + 1024 + coef1 * hist1 + coef2 * hist2) >> 11
            value = max(-32768, min(32767, value))
            hist2, hist1 = hist1, value
            pcm.append(value)
        at += FRAME_BYTES
    return pcm, rate


def build_clip(entries, name, level, trim):
    pcm, rate = decode_dsp(find_entry(entries, name))
    if trim:
        end = len(pcm)
        while end > 0 and abs(pcm[end - 1]) < SILENCE_LEVEL:
            end -= 1
        pcm = pcm[:end]
    gain = 1.0
    if level:
        peak = max(abs(v) for v in pcm)
        gain = min(MAX_VOICE_GAIN, VOICE_PEAK / peak)
        pcm = [int(round(v * gain)) for v in pcm]
    return pcm, rate, gain


def request_table(entries, needle):
    """Bank request id to wave name: SRQR entries (0x90 bytes from 0x48) name a wave inside an SBKR group."""
    bank = {raw[:4]: raw for name, raw in entries if needle in name and raw[:4] in (b"SBKR", b"SRQR")}
    if b"SBKR" not in bank or b"SRQR" not in bank:
        fail("bank metadata missing for " + needle)
    sbkr, srqr = bank[b"SBKR"], bank[b"SRQR"]
    names = [m.group().decode().split("\\")[-1] for m in re.finditer(rb"app\\[ -~]+", sbkr)]
    starts = {}
    for i in range(struct.unpack_from("<I", sbkr, 8)[0]):
        at = 0x14 + i * 0x18
        starts[sbkr[at]] = struct.unpack_from("<I", sbkr, at + 8)[0]
    table = {}
    for i in range(struct.unpack_from("<I", srqr, 8)[0]):
        entry = srqr[0x48 + i * 0x90:0x48 + (i + 1) * 0x90]
        request, wave, enabled, group = entry[0], entry[2], entry[8], entry[0x1E]
        if wave == 0xFF or enabled == 0 or group not in starts or starts[group] + wave >= len(names):
            continue
        table[request] = names[starts[group] + wave]
    return table


def write_event_map(folder, snd):
    tables = {
        "evil_ngs": request_table(read_arc(snd / "chr18_se_ngs.arc"), "chr18\\snd_se"),
        "evil_org": request_table(read_arc(snd / "chr18_se_org.arc"), "chr18\\snd_se"),
        "ryu_ngs": request_table(read_arc(snd / "chr01_se_ngs.arc"), "chr01\\snd_se"),
        "ryu_org": request_table(read_arc(snd / "chr01_se_org.arc"), "chr01\\snd_se"),
    }
    rows = []
    for request in sorted(tables["evil_ngs"]):
        wave = tables["evil_ngs"][request]
        if wave == "blank" or request >= 50:
            continue
        event, hd = HD_EVENTS.get(request, ("unmapped", "no HD Remix Ryu event"))
        rows.append({
            "request": request,
            "evil_wave": wave,
            "evil_org_wave": tables["evil_org"].get(request, ""),
            "ryu_wave": tables["ryu_ngs"].get(request, ""),
            "ryu_org_wave": tables["ryu_org"].get(request, ""),
            "event": event,
            "hd_remix_sound": hd,
            "strategy": "suppress Ryu clip, play Evil Ryu clip on the same side" if request in HD_EVENTS else "not played",
        })
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "event_map.json").write_text(json.dumps(rows, indent=2) + "\n", newline="\n")
    lines = ["request  evil wave                       ryu wave              event            hd remix sound"]
    for row in rows:
        lines.append("%7d  %-30s  %-20s  %-15s  %s" % (row["request"], row["evil_wave"], row["ryu_wave"], row["event"],
                                                       row["hd_remix_sound"]))
    (folder / "event_map.txt").write_text("\n".join(lines) + "\n", newline="\n")


def build(snd, out, analysis):
    voice = read_arc(snd / VOICE_BANK)
    common = read_arc(snd / COMMON_BANK)
    records = []
    for clip_id, name, archive, entry, level, trim in CLIPS:
        pcm, rate, gain = build_clip(voice if archive == VOICE_BANK else common, entry, level, trim)
        records.append(struct.pack("<3I", clip_id, rate, len(pcm)) + struct.pack("<%dh" % len(pcm), *pcm))
        print("%-16s %6d samples %5d Hz gain %.3f" % (name, len(pcm), rate, gain))
    out.parent.mkdir(parents=True, exist_ok=True)
    package = MAGIC + struct.pack("<I", len(records)) + b"".join(records)
    out.write_bytes(package)
    write_event_map(analysis, snd)
    print("wrote", out, len(package), "bytes sha256", hashlib.sha256(package).hexdigest())


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xci", type=Path, help="USF2 USA XCI or NSP")
    parser.add_argument("--keys", type=Path, help="prod.keys")
    parser.add_argument("--snd-dir", type=Path, help="folder that already holds the extracted snd archives")
    parser.add_argument("--out", type=Path, default=root / "game/extracted/evil_ryu_audio.bin")
    parser.add_argument("--analysis", type=Path, default=root / "out/evil-ryu-analysis/usf2/audio")
    parser.add_argument("--kit", type=Path, default=root / "game/references/usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    args = parser.parse_args()

    if args.snd_dir:
        build(args.snd_dir, args.out, args.analysis)
        return
    if not args.xci or not args.keys:
        fail("pass --xci and --keys, or --snd-dir")
    sys.path.insert(0, str(args.kit))
    import switch_source

    pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
    nstool = args.nstool or find_nstool()
    with tempfile.TemporaryDirectory(prefix="evil-ryu-audio-") as work:
        with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
            switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(SOUND_FOLDER), Path(work) / "snd")
        build(Path(work) / "snd", args.out, args.analysis)


if __name__ == "__main__":
    main()
