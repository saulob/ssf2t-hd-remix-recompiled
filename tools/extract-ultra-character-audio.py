#!/usr/bin/env python3
"""Builds the voice and announcer packages of the USF2 characters from the user's own USF2 dump.

The output is derived from the user's game data and belongs next to the HD Remix game files. It must not be
committed or redistributed. Nothing is played; the tool only reads the dump and writes the package.

USF2 wave entries are Nintendo DSP ADPCM: 0x08 u32 data bytes, 0x0C f32 seconds, 0x18 16 s16 coefficients,
0x60 u32 samples, 0x64 u32 rate, 0x68 u32 loop flag, 0x70 u32 last sample, 0x78 8-byte frames of 14 samples.

The voice call of USF2 takes an index into snd_chrNN_param (u32 0, u32 count, then u32 request, u32 0), not a request.
SRQR then names the wave, and only the org bank spells out what a voice says. A throw makes two calls, one each for the
ngs and the org sound mode, with different indexes: request 4 is the org throw, and in the ngs bank a kick voice.

Package (little endian): 8-byte magic, u32 count, then per clip u32 id, u32 rate, u32 samples, s16 mono samples.
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

SOUND_FOLDER = "/1/nativeNXx64/app/arc_cmn/snd"
WAVE_HEADER = bytes.fromhex("0200000001000001")
DATA_OFFSET = 0x78
FRAME_BYTES = 8
FRAME_SAMPLES = 14
SILENCE_LEVEL = 200
VOICE_PEAK = 29000
MAX_VOICE_GAIN = 2.0

COMMON_BANK = "common_se_ngs.arc"
COMMON_FOLDER = "app\\sound\\se\\ngs\\com\\wav\\voice\\ngs\\"


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


def voice_calls(entries, needle):
    """Index the code passes to the voice call, in order, to the bank request it plays."""
    for name, raw in entries:
        if needle in name:
            zero, count = struct.unpack_from("<II", raw, 0)
            if zero != 0 or len(raw) < 8 + count * 8:
                fail("unexpected request parameters in " + name)
            return [struct.unpack_from("<I", raw, 8 + i * 8)[0] for i in range(count)]
    fail("request parameters missing for " + needle)


def write_table(folder, rows, lines):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "event_map.json").write_text(json.dumps(rows, indent=2) + "\n", newline="\n")
    (folder / "event_map.txt").write_text("\n".join(lines) + "\n", newline="\n")


class EvilRyu:
    magic = b"SSF2ERV1"
    package = "evil_ryu_audio.bin"
    analysis = "out/evil-ryu-analysis/usf2/audio"
    temp_prefix = "evil-ryu-audio-"
    bank = "chr18_se_ngs.arc"
    folder = "app\\sound\\se\\ngs\\chr18\\wav\\"

    # (package id, name, archive, entry, level the clip like a character voice, drop trailing silence)
    clips = (
        (1, "hadoken", bank, folder + "se_ng_chr18_v_28", True, False),
        (2, "shoryuken", bank, folder + "se_ng_chr18_v_29", True, False),
        (3, "tatsumaki", bank, folder + "se_ng_chr18_v_30", True, False),
        (4, "ko", bank, folder + "se_ng_chr18_v_69", True, False),
        (5, "shun_goku_satsu", bank, folder + "se_ng_chr18_v_senku", True, False),
        (6, "announcer_name", COMMON_BANK, COMMON_FOLDER + "se_v_com_evil_ryu_ng_jp", False, True),
    )

    # HD Remix sound ids of the Ryu events each USF2 request replaces.
    hd_events = {
        0: ("KO", "P1 0x64 (100), P2 0x13A (314); end of round 100, side from the loser in fight work +1356"),
        1: ("Hadoken", "P1 0x61 (97), P2 0x137 (311)"),
        2: ("Shoryuken", "P1 0x60 (96), P2 0x136 (310)"),
        3: ("Tatsumaki", "P1 0x62 (98), P2 0x138 (312)"),
        12: ("Shun Goku Satsu", "host move start, carried on the P1 or P2 Hadoken id"),
    }

    @classmethod
    def resolve(cls, snd):
        return cls.clips

    @classmethod
    def write_maps(cls, root, analysis, snd):
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
            event, hd = cls.hd_events.get(request, ("unmapped", "no HD Remix Ryu event"))
            rows.append({
                "request": request,
                "evil_wave": wave,
                "evil_org_wave": tables["evil_org"].get(request, ""),
                "ryu_wave": tables["ryu_ngs"].get(request, ""),
                "ryu_org_wave": tables["ryu_org"].get(request, ""),
                "event": event,
                "hd_remix_sound": hd,
                "strategy": "suppress Ryu clip, play Evil Ryu clip on the same side" if request in cls.hd_events else "not played",
            })
        lines = ["request  evil wave                       ryu wave              event            hd remix sound"]
        for row in rows:
            lines.append("%7d  %-30s  %-20s  %-15s  %s" % (row["request"], row["evil_wave"], row["ryu_wave"], row["event"],
                                                           row["hd_remix_sound"]))
        write_table(analysis, rows, lines)


class ViolentKen:
    magic = b"SSF2VKV1"
    package = "violent_ken_audio.bin"
    analysis = "out/violent-ken-analysis/usf2/audio"
    hd_analysis = "out/violent-ken-analysis/audio"
    temp_prefix = "violent-ken-audio-"
    bank = "chr19_se_ngs.arc"
    needle = "chr19\\snd_se"
    folder = "app\\sound\\se\\ngs\\chr19\\wav\\"
    announcer = "se_v_com_vio_ken_ng"

    # (package id, name, bank request, word the org bank must use for that request, HD event, P1 id, P2 id, swapped ids)
    voices = (
        (1, "hadoken", 2, "hadoken", "Hadoken", "102", "316", "0xD1, 0xD7"),
        (2, "shoryuken", 1, "shoryuken", "Shoryuken", "101", "315", "0xD0, 0xD6"),
        (3, "tatsumaki", 3, "tatsumaki", "Tatsumaki", "103", "317", "0xD2, 0xD8"),
        (4, "throw", 24, None, "Throw", "104", "318", "0xD3, 0xD9"),
        (5, "ko", 0, "dead", "KO", "105 (hit KO P1; end of round, side from the loser)", "319 (hit KO P2)", "0xDC, 0xDD"),
        (6, "rasetsu", 11, None, "Rasetsukyaku", "host move, carried on 102", "host move, carried on 316", "0xD4, 0xDA"),
        (7, "shinbu", 20, None, "Shinbu Messatsu", "host move, carried on 102", "host move, carried on 316", "0xD5, 0xDB"),
    )
    voice_index_of = {"throw": 11, "rasetsu": 6, "shinbu": 7}
    org_throw_request = 4

    @classmethod
    def resolve(cls, snd):
        ngs = request_table(read_arc(snd / cls.bank), cls.needle)
        org = request_table(read_arc(snd / "chr19_se_org.arc"), cls.needle)
        calls = voice_calls(read_arc(snd / cls.bank), "chr19\\snd_chr19_param")
        if "nage" not in org.get(cls.org_throw_request, ""):
            fail("org bank does not call request %d nage" % cls.org_throw_request)
        clips = []
        for clip_id, name, request, word, *_ in cls.voices:
            if request not in ngs or ngs[request] == "blank":
                fail("no wave for request %d" % request)
            if word and word not in org.get(request, ""):
                fail("org bank does not call request %d %s" % (request, word))
            if name in cls.voice_index_of and calls[cls.voice_index_of[name]] != request:
                fail("voice call %d no longer plays request %d" % (cls.voice_index_of[name], request))
            clips.append((clip_id, name, cls.bank, cls.folder + ngs[request], True, False))
        clips.append((8, "announcer_name", COMMON_BANK, COMMON_FOLDER + cls.announcer, False, True))
        return clips

    @classmethod
    def write_maps(cls, root, analysis, snd):
        vio = read_arc(snd / cls.bank)
        ngs = request_table(vio, cls.needle)
        org = request_table(read_arc(snd / "chr19_se_org.arc"), cls.needle)
        ken = request_table(read_arc(snd / "chr05_se_ngs.arc"), "chr05\\snd_se")
        ken_org = request_table(read_arc(snd / "chr05_se_org.arc"), "chr05\\snd_se")
        calls = voice_calls(vio, "chr19\\snd_chr19_param")
        events = {voice[2]: voice for voice in cls.voices}
        rows = []
        for index, request in enumerate(calls):
            wave = ngs.get(request, "")
            if request >= 50 or wave in ("", "blank"):
                continue
            voice = events.get(request)
            rows.append({
                "voice_call": index,
                "request": request,
                "violent_wave": wave,
                "violent_org_wave": org.get(request, ""),
                "ken_wave": ken.get(request, ""),
                "ken_org_wave": ken_org.get(request, ""),
                "event": voice[4] if voice else "unmapped",
                "hd_remix_sound": "P1 %s, P2 %s" % (voice[5], voice[6]) if voice else "no HD Remix Ken event",
                "strategy": "suppress Ken clip, play Violent Ken clip on the same side" if voice else "not played",
            })
        lines = ["call  request  violent wave              ken wave              event            hd remix sound"]
        for row in rows:
            lines.append("%4d  %7d  %-24s  %-20s  %-15s  %s" % (row["voice_call"], row["request"], row["violent_wave"],
                                                                 row["ken_wave"], row["event"], row["hd_remix_sound"]))
        write_table(analysis, rows, lines)

        hd = [{
            "hd_event": voice[4],
            "ken_sound_p1": voice[5],
            "ken_sound_p2": voice[6],
            "violent_sound": voice[7],
            "clip": ngs[voice[2]],
        } for voice in cls.voices]
        hd.append({
            "hd_event": "Announcer name",
            "ken_sound_p1": "465",
            "ken_sound_p2": "465",
            "violent_sound": "0xDE",
            "clip": cls.announcer,
        })
        folder = root / cls.hd_analysis
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "event_map.json").write_text(json.dumps(hd, indent=2) + "\n", newline="\n")


CHARACTERS = {"evil_ryu": EvilRyu, "violent_ken": ViolentKen}


def build(character, root, snd, out, analysis):
    archives = {}
    records = []
    for clip_id, name, archive, entry, level, trim in character.resolve(snd):
        entries = archives.setdefault(archive, read_arc(snd / archive))
        pcm, rate, gain = build_clip(entries, entry, level, trim)
        records.append(struct.pack("<3I", clip_id, rate, len(pcm)) + struct.pack("<%dh" % len(pcm), *pcm))
        print("%-16s %6d samples %5d Hz gain %.3f" % (name, len(pcm), rate, gain))
    out.parent.mkdir(parents=True, exist_ok=True)
    package = character.magic + struct.pack("<I", len(records)) + b"".join(records)
    out.write_bytes(package)
    character.write_maps(root, analysis, snd)
    print("wrote", out, len(package), "bytes sha256", hashlib.sha256(package).hexdigest())


def main(name=None):
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    if name is None:
        parser.add_argument("--character", choices=sorted(CHARACTERS), required=True)
    parser.add_argument("--xci", type=Path, help="USF2 USA XCI or NSP")
    parser.add_argument("--keys", type=Path, help="prod.keys")
    parser.add_argument("--snd-dir", type=Path, help="folder that already holds the extracted snd archives")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--analysis", type=Path)
    parser.add_argument("--kit", type=Path, default=root / "game/references/usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    args = parser.parse_args()

    character = CHARACTERS[name or args.character]
    out = args.out or root / "assets/characters" / character.package
    analysis = args.analysis or root / character.analysis
    if args.snd_dir:
        build(character, root, args.snd_dir, out, analysis)
        return
    if not args.xci or not args.keys:
        fail("pass --xci and --keys, or --snd-dir")
    sys.path.insert(0, str(args.kit))
    import switch_source

    pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
    nstool = args.nstool or find_nstool()
    with tempfile.TemporaryDirectory(prefix=character.temp_prefix) as work:
        with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
            switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(SOUND_FOLDER), Path(work) / "snd")
        build(character, root, Path(work) / "snd", out, analysis)


if __name__ == "__main__":
    main()
