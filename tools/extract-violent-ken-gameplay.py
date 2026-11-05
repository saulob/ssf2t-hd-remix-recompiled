#!/usr/bin/env python3
"""Builds violent_ken_gameplay.bin and the Violent Ken analysis manifests from the user's own USF2 dump.

USF2 keeps Violent Ken's frames in its executable and his attack table in data built by static initialisers, so this
tool runs those initialisers in Unicorn. Nothing here launches the game. The output is derived from the user's game
data, belongs in the game data folder and must not be committed or redistributed.

Package (little endian): "SSF2VKG1", u32 action count, u32 patch count, u32 attack count, then
  actions: u8 action, u8 sub, u8 frame count, u8 reserved, then per frame u16 duration, u16 event, u16 source key,
           u8 head, body, legs, attack, push, shape, weight, art (1 = the Violent Ken stance sprite)
  patches: u8 action, u8 sub, u8 frame, u8 field (0 duration, 1 head, 2 body, 3 legs, 4 attack), u8 expected, u8 value,
           u16 reserved
  attacks: u8 id, u8 replace flag (0 append, 1 overwrite), u8[16] guest record
"""
import argparse
import collections
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

MAGIC = b"SSF2VKG1"
EXEFS = "/0"
KEN, VIOLENT = 5, 19
KEN_ACTIONS, VIOLENT_ACTIONS = 0xE7D850, 0xF49428
KEN_ANIMS, VIOLENT_ANIMS = (0xE877F8, 0xA8), (0xF54668, 0xAF)
KEN_PARTS, VIOLENT_PARTS = (0xE887B8, 263), (0xF556D0, 270)
VIOLENT_GEOMETRY = 0xBD9864
KEN_ATTACKS, VIOLENT_ATTACKS = 0x100EF00, 0x1059E70
ATTACK_INTS = 15
ATTACK_RECORD = ATTACK_INTS * 4
CLASS_TABLE = 0xE16AC8
CATEGORIES = 0x57
VIOLENT_ONLY_POSE = 280
# Attack byte 11 is the guest's move class, an even index into the dispatch table at 83A2A6B0 (41 entries).
LAST_ATTACK_CLASS = 0x50

# Actions with no guest slot. The move hook starts them directly; their ids are outside the guest's action range.
INAZUMA, RASETSU, SHINBU = 0xE0, 0xE2, 0xE4
FAR_FORWARD, CLOSE_ROUNDHOUSE = (0x42, 3), (0x42, 4)
NEW_ACTIONS = {
    35: FAR_FORWARD, 37: CLOSE_ROUNDHOUSE, 36: (INAZUMA, 0), 44: (RASETSU, 0), 45: (RASETSU, 1), 46: (RASETSU, 2),
    60: (SHINBU, 0), 61: (SHINBU, 1), 62: (SHINBU, 2), 55: (SHINBU, 3), 63: (SHINBU, 4),
}
# Ken's walk differs between USF2 and HD Remix in art order, so the sequence match cannot find its slot.
WALK_ANIMATION, WALK_SLOT = 98, (0, 0)
# HD Remix never drew this USF2 pose; the next frame of the same move stands in for it.
POSE_SUBSTITUTE = {38: 42}
FIELDS = {"dur": (1, 0), "head": (5, 1), "body": (6, 2), "legs": (7, 3), "atk": (8, 4)}


def fail(message):
    raise SystemExit("error: " + message)


def find_nstool():
    base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    for pattern in ("capcom/switch-helpers/*/*/bin/nstool*", "capcom/switch-helpers/*/bin/nstool*"):
        found = sorted(base.glob(pattern))
        if found:
            return found[0]
    fail("nstool not found; pass --nstool")


def lz4(source, size):
    out = bytearray()
    at = 0
    while len(out) < size:
        token = source[at]
        at += 1
        literals = token >> 4
        if literals == 15:
            while True:
                extra = source[at]
                at += 1
                literals += extra
                if extra != 255:
                    break
        out += source[at:at + literals]
        at += literals
        if len(out) >= size:
            break
        offset = source[at] | source[at + 1] << 8
        at += 2
        length = token & 15
        if length == 15:
            while True:
                extra = source[at]
                at += 1
                length += extra
                if extra != 255:
                    break
        length += 4
        start = len(out) - offset
        for i in range(length):
            out.append(out[start + i])
    return bytes(out)


def load_nso(path):
    data = Path(path).read_bytes()
    if data[:4] != b"NSO0":
        fail("main is not an NSO")
    flags = struct.unpack_from("<I", data, 0xC)[0]
    segments = []
    for index in range(3):
        file_offset, memory, size = struct.unpack_from("<III", data, 0x10 + index * 16)
        compressed = struct.unpack_from("<I", data, 0x60 + index * 4)[0]
        segments.append((file_offset, memory, size, compressed))
    bss = struct.unpack_from("<I", data, 0x3C)[0]
    image = bytearray(max(m + s for _, m, s, _ in segments) + bss + 0x1000)
    for index, (file_offset, memory, size, compressed) in enumerate(segments):
        blob = data[file_offset:file_offset + compressed]
        image[memory:memory + size] = lz4(blob, size) if flags & (1 << index) else blob
    return bytes(image)


def dynamic_tags(image):
    module = struct.unpack_from("<I", image, 4)[0]
    at = module + struct.unpack_from("<i", image, module + 4)[0]
    tags = {}
    while True:
        tag, value = struct.unpack_from("<QQ", image, at)
        at += 16
        if tag == 0:
            return tags
        tags[tag] = value


def initialised_image(image):
    """Applies relocations and runs every static initialiser, which is where the attack tables are built."""
    from unicorn import Uc, UC_ARCH_ARM64, UC_HOOK_CODE, UC_MODE_ARM, UcError
    from unicorn.arm64_const import UC_ARM64_REG_LR, UC_ARM64_REG_SP, UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

    tags = dynamic_tags(image)
    memory = bytearray(image)
    stub_base, stack, heap = 0x40000000, 0x50000000, 0x60000000
    stubs = {}
    relocations = []
    if 7 in tags:
        relocations += [struct.unpack_from("<QQq", image, tags[7] + i * 24) for i in range(tags[8] // 24)]
    if 23 in tags:
        relocations += [struct.unpack_from("<QQq", image, tags[23] + i * 24) for i in range(tags[2] // 24)]
    for offset, info, addend in relocations:
        if info & 0xFFFFFFFF == 0x403:
            struct.pack_into("<Q", memory, offset, addend)
            continue
        name_offset, _, _, section, value, _ = struct.unpack_from("<IBBHQQ", image, tags[6] + (info >> 32) * 24)
        if section:
            struct.pack_into("<Q", memory, offset, value + addend)
            continue
        end = image.index(b"\0", tags[5] + name_offset)
        name = image[tags[5] + name_offset:end].decode("latin-1")
        stubs.setdefault(name, stub_base + len(stubs) * 4)
        struct.pack_into("<Q", memory, offset, stubs[name])
    names = {address: name for name, address in stubs.items()}
    emulator = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    emulator.mem_map(0, (len(memory) + 0xFFF) & ~0xFFF)
    emulator.mem_write(0, bytes(memory))
    emulator.mem_map(stub_base, 0x100000)
    emulator.mem_write(stub_base, b"\xc0\x03\x5f\xd6" * 0x40000)
    emulator.mem_map(stack, 0x100000)
    emulator.mem_map(heap, 0x4000000)
    next_heap = [heap]

    def on_stub(uc, address, size, _):
        name = names.get(address, "")
        x0, x1, x2 = (uc.reg_read(r) for r in (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2))
        if name in ("memcpy", "memmove"):
            uc.mem_write(x0, bytes(uc.mem_read(x1, x2)))
        elif name == "memset":
            uc.mem_write(x0, bytes([x1 & 255]) * x2)
        elif name == "__cxa_guard_acquire":
            uc.reg_write(UC_ARM64_REG_X0, 1)
        elif name in ("malloc", "_Znwm", "_Znam"):
            uc.reg_write(UC_ARM64_REG_X0, next_heap[0])
            next_heap[0] += (x0 + 31) & ~15
        else:
            uc.reg_write(UC_ARM64_REG_X0, 0)

    emulator.hook_add(UC_HOOK_CODE, on_stub, begin=stub_base, end=stub_base + 0xFFFFF)
    done = stub_base + 0xFFFF0
    for index in range(tags[27] // 8):
        entry = struct.unpack_from("<Q", bytes(emulator.mem_read(tags[25] + index * 8, 8)))[0]
        if not entry:
            continue
        emulator.reg_write(UC_ARM64_REG_SP, stack + 0xF0000)
        emulator.reg_write(UC_ARM64_REG_LR, done)
        emulator.reg_write(UC_ARM64_REG_X0, 0)
        try:
            emulator.emu_start(entry, done, timeout=10_000_000, count=3_000_000)
        except UcError as error:
            fail("static initialiser %d failed: %s" % (index, error))
    return bytes(emulator.mem_read(0, len(image)))


class Usf2:
    def __init__(self, memory):
        self.memory = memory

    def u64(self, address):
        return struct.unpack_from("<Q", self.memory, address)[0]

    def actions(self, table):
        out = []
        for index in range(CATEGORIES):
            pointer, size = self.u64(table + index * 16), self.u64(table + index * 16 + 8)
            out.append(list(struct.unpack_from("<%di" % (size // 4), self.memory, pointer)) if pointer else [])
        return out

    def animations(self, table, count):
        out = {}
        for index in range(count):
            at = table + index * 0x18
            identity = struct.unpack_from("<I", self.memory, at)[0]
            pointer, size = self.u64(at + 8), struct.unpack_from("<I", self.memory, at + 16)[0]
            if pointer:
                out[identity] = [list(struct.unpack_from("<14i", self.memory, pointer + i * 56))
                                 for i in range(size // 56)]
        return out

    def parts(self, table, count):
        out = {}
        for index in range(count):
            at = table + index * 0x18
            pointer = self.u64(at + 8)
            end = self.memory.index(b"\0", pointer)
            out[struct.unpack_from("<i", self.memory, at)[0]] = self.memory[pointer:end].decode("latin-1")
        return out

    def attack(self, table, identity):
        return struct.unpack_from("<%di" % ATTACK_INTS, self.memory, table + identity * ATTACK_RECORD)


def guest_attack(record):
    """USF2 attack record to the guest's 16 byte record; the layout is proven against guest Ken in the report."""
    x, y, w, h, stun, flags, f6, f7, f8, f9, f10, f11, f12, _, packed = record
    special = (flags & 1) << 7
    out = struct.pack("4b", x, y, w, h)
    out += bytes(((stun & 0x7F) | special, f6 & 0xFF, (0x27 + f7) & 0xFF, f8 & 0xFF, f9 & 0xFF, f10 & 0xFF, f11 & 0xFF,
                  max(f12, 0) & 0xFF))
    b1, b2, b3 = (packed >> 8) & 0xFF, (packed >> 16) & 0xFF, (packed >> 24) & 0xFF
    return out + bytes((0, (b1 << 4 | b2) & 0xFF, (b3 << 7) & 0xFF, 0))


class Fpg:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        count = struct.unpack_from("<I", self.data, 4)[0]
        self.entries = {}
        for index in range(count):
            key, offset, size, _ = struct.unpack_from("<IIII", self.data, 0x800 + index * 16)
            self.entries[key] = (offset, size)

    def read(self, name):
        entry = self.entries.get(zlib.crc32(name.replace("\\", "/").upper().encode("latin-1")))
        if not entry:
            fail("missing " + name + " in the game archive")
        raw = self.data[entry[0]:entry[0] + entry[1]]
        try:
            return zlib.decompress(raw)
        except zlib.error:
            return raw


def guest_tables(fpg):
    rows = [line.split("\t") for line in fpg.read("textures/characters/Ken/Ken.tsv").decode("latin-1").splitlines()]
    geometry = {int(r[1]): (r[7].split("\\")[-1], int(r[2]), int(r[3]), int(r[5]), int(r[6]))
                for r in rows if r[0] == "ca04"}
    table = fpg.read("pl04tbl0.bin")[16:]
    word = lambda at: struct.unpack_from("<H", table, at)[0]
    header = word(0)
    starts = sorted({word(a) for a in range(0, header, 2)})
    first = min(word(a) + word(word(a)) for a in range(0, header, 2))
    actions = {}
    for action in range(0, header, 2):
        sub_table = word(action)
        following = min([s for s in starts if s > sub_table] + [first])
        for sub in range((following - sub_table) // 2):
            record = sub_table + word(sub_table + sub * 2)
            if record < first or record + 24 > len(table) or record - first not in geometry:
                continue
            frames = []
            at = record
            for _ in range(80):
                flags = word(at + 2)
                frames.append({"key": at - first, "dur": word(at), "head": table[at + 8], "body": table[at + 9],
                               "legs": table[at + 10], "atk": table[at + 12], "push": table[at + 13]})
                if flags & 0x8000:
                    break
                at += 24
            actions[(action, sub)] = frames
    return geometry, actions


def hd_name(texture):
    return "Ken_" + texture.split("_", 1)[1]


def source_keys(geometry, names, pose_geometry):
    by_texture = collections.defaultdict(list)
    for key, entry in sorted(geometry.items()):
        by_texture[entry[0]].append(key)
    out = {}
    for pose, texture in names.items():
        candidates = by_texture.get(hd_name(texture), [])
        if not candidates:
            continue
        centre, base = pose_geometry.get(pose, (None, None))
        exact = [k for k in candidates if centre is not None and geometry[k][1] == int(centre - geometry[k][3] // 2)
                 and geometry[k][2] == int(base - geometry[k][4])]
        out[pose] = (exact or candidates)[0]
    return out


def write_package(path, actions, patches, attacks):
    out = bytearray(MAGIC + struct.pack("<3I", len(actions), len(patches), len(attacks)))
    for (action, sub), frames in actions:
        out += bytes((action, sub, len(frames), 0))
        for f in frames:
            out += struct.pack("<3H8B", f["dur"], f["event"], f["key"], f["head"], f["body"], f["legs"], f["atk"],
                               f["push"], f["w10"], f["w11"], f["art"])
    for action, sub, frame, field, expected, value in patches:
        out += bytes((action, sub, frame, field, expected, value, 0, 0))
    for identity, replace, record in attacks:
        out += bytes((identity, replace)) + record
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    references = root / "game/references"
    parser.add_argument("--xci", type=Path,
                        default=references / "Ultra Street Fighter II - The Final Challengers (USA) (En,Fr,De,Es,It).xci")
    parser.add_argument("--keys", type=Path, default=references / "prod.keys")
    parser.add_argument("--main", type=Path, help="an already extracted ExeFS main")
    parser.add_argument("--fpg", type=Path, default=root / "game/extracted/flog_x360.fpg")
    parser.add_argument("--kit", type=Path, default=references / "usf2-backport-rc5-kit")
    parser.add_argument("--nstool", type=Path)
    parser.add_argument("--out", type=Path, default=root / "game/extracted/violent_ken_gameplay.bin")
    parser.add_argument("--analysis", type=Path, default=root / "out/violent-ken-analysis")
    args = parser.parse_args()

    if args.main:
        image = load_nso(args.main)
    else:
        sys.path.insert(0, str(args.kit))
        import switch_source

        pin = json.loads((args.kit / "kit.json").read_text())["nsp"]["us"]
        nstool = args.nstool or find_nstool()
        with tempfile.TemporaryDirectory(prefix="violent-ken-") as work:
            with switch_source.open_program(args.xci, pin, work) as (program, ticket, _):
                switch_source.extract(nstool, args.keys, program, ticket, PurePosixPath(EXEFS), Path(work) / "exefs")
            image = load_nso(Path(work) / "exefs/main")

    usf2 = Usf2(initialised_image(image))
    ken_actions, violent_actions = usf2.actions(KEN_ACTIONS), usf2.actions(VIOLENT_ACTIONS)
    ken_anims, violent_anims = usf2.animations(*KEN_ANIMS), usf2.animations(*VIOLENT_ANIMS)
    ken_names, violent_names = usf2.parts(*KEN_PARTS), usf2.parts(*VIOLENT_PARTS)
    if violent_names.get(VIOLENT_ONLY_POSE) != "chr19_200_00":
        fail("pose 280 is not the Violent Ken sprite; this is not the pinned executable")

    pose_geometry = {}
    for index in range(270):
        identity, centre, base = struct.unpack_from("<iff", image, VIOLENT_GEOMETRY + index * 0x1C)
        pose_geometry[identity] = (centre, base)

    geometry, guest_actions = guest_tables(Fpg(args.fpg))
    keys = source_keys(geometry, violent_names, pose_geometry)

    by_sequence = collections.defaultdict(list)
    for slot, frames in guest_actions.items():
        by_sequence[tuple(geometry[f["key"]][0] for f in frames)].append(slot)
    guest_slots = {}
    for identity, frames in ken_anims.items():
        sequence = tuple(hd_name(ken_names[f[3]]) if f[3] in ken_names else "?" for f in frames)
        if frames and sequence in by_sequence:
            guest_slots[identity] = by_sequence[sequence]

    guest_slots.setdefault(WALK_ANIMATION, [WALK_SLOT])

    pairs = {}
    for ken_ids, violent_ids in zip(ken_actions, violent_actions):
        if len(ken_ids) == len(violent_ids):
            for ken_id, violent_id in zip(ken_ids, violent_ids):
                pairs.setdefault(violent_id, ken_id)
    for identity in NEW_ACTIONS:
        pairs.pop(identity, None)

    actions = []
    missing_art = set()
    for identity, slot in sorted(NEW_ACTIONS.items()):
        frames = []
        for f in violent_anims[identity]:
            pose = f[3]
            art = 1 if pose == VIOLENT_ONLY_POSE else 0
            if pose not in keys and not art:
                missing_art.add(pose)
            key = keys.get(POSE_SUBSTITUTE.get(pose, pose), keys.get(POSE_SUBSTITUTE.get(pose, 0), 0))
            frames.append({"dur": f[1], "event": f[2] & 0xFF, "key": key, "head": f[5], "body": f[6], "legs": f[7],
                           "atk": f[8], "push": f[9], "w10": f[10] & 0xFF, "w11": min(f[11], 255), "art": art})
        actions.append((slot, frames))

    patches = []
    skipped = []
    modified = []
    for violent_id, ken_id in sorted(pairs.items()):
        ken_frames, violent_frames = ken_anims.get(ken_id, []), violent_anims.get(violent_id, [])
        if ken_frames == violent_frames:
            continue
        slots = guest_slots.get(ken_id, [])
        modified.append({"ken": ken_id, "violent": violent_id, "guest": [["0x%02X" % a, s] for a, s in slots],
                         "ken_frames": ken_frames, "violent_frames": violent_frames})
        if len(ken_frames) != len(violent_frames):
            for action, sub in slots:
                frames = []
                for f in violent_frames:
                    key = keys.get(f[3], 0)
                    frames.append({"dur": f[1], "event": f[2] & 0xFF, "key": key, "head": f[5], "body": f[6],
                                   "legs": f[7], "atk": f[8], "push": f[9], "w10": f[10] & 0xFF,
                                   "w11": min(f[11], 255), "art": 1 if f[3] == VIOLENT_ONLY_POSE else 0})
                if frames:
                    actions.append(((action, sub), frames))
            if not slots:
                skipped.append({"ken": ken_id, "violent": violent_id, "reason": "no guest slot"})
            continue
        for index, (a, b) in enumerate(zip(ken_frames, violent_frames)):
            for field, (column, code) in FIELDS.items():
                if a[column] == b[column]:
                    continue
                if field == "atk" and b[column] != 0:
                    skipped.append({"ken": ken_id, "violent": violent_id, "frame": index,
                                    "reason": "attack id change on a shared record"})
                    continue
                for action, sub in slots:
                    patches.append((action, sub, index, code, a[column] & 0xFF, b[column] & 0xFF))
        if not slots:
            skipped.append({"ken": ken_id, "violent": violent_id, "reason": "no guest slot"})

    attacks = []
    attack_report = []
    fights = Fpg(args.fpg).read("s_data04.bin")[16:]
    guest_base = struct.unpack_from("<H", fights, 6)[0]
    layout_matches = 0
    for identity in range(1, 74):
        if guest_attack(usf2.attack(KEN_ATTACKS, identity))[:12] == fights[guest_base + identity * 16:][:12]:
            layout_matches += 1
    for identity in range(1, 80):
        violent = usf2.attack(VIOLENT_ATTACKS, identity)
        ken = usf2.attack(KEN_ATTACKS, identity) if identity < 74 else None
        record = bytearray(guest_attack(violent))
        changed = ken is None or [v for i, v in enumerate(violent) if i != 5] != [v for i, v in enumerate(ken) if i != 5]
        if not changed:
            continue
        kept_effect = False
        if record[11] > LAST_ATTACK_CLASS or record[11] & 1:
            kept_effect = True
            record[11] = fights[guest_base + identity * 16 + 11] if identity < 74 else 0
        attacks.append((identity, 1 if identity < 74 else 0, bytes(record)))
        attack_report.append({"id": identity, "violent": list(violent), "ken": list(ken) if ken else None,
                              "guest": record.hex(), "effect_clamped": kept_effect})

    write_package(args.out, actions, patches, attacks)

    analysis = args.analysis
    (analysis / "gameplay").mkdir(parents=True, exist_ok=True)
    (analysis / "usf2").mkdir(parents=True, exist_ok=True)
    (analysis / "gameplay/action_map.json").write_text(json.dumps({
        "new": {str(i): {"slot": ["0x%02X" % NEW_ACTIONS[i][0], NEW_ACTIONS[i][1]], "frames": violent_anims[i]}
                for i in NEW_ACTIONS},
        "modified": modified, "patches": len(patches), "skipped": skipped,
        "attack_layout_matches_guest_ken": "%d/73" % layout_matches, "attacks": attack_report,
        "missing_art": sorted(missing_art)}, indent=1))
    (analysis / "usf2/ken_actions.json").write_text(json.dumps({"ken": ken_actions, "violent": violent_actions}))
    print("wrote", args.out, "actions", len(actions), "patches", len(patches), "attacks", len(attacks))
    print("attack layout matches guest Ken: %d/73" % layout_matches, "skipped", len(skipped), "missing art", sorted(missing_art))


if __name__ == "__main__":
    main()
