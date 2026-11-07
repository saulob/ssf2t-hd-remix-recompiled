#!/usr/bin/env python3
"""Unlocks the USF2 CPS-2 backport kit with the user's own USF2 dump and writes a patch manifest.

The kit stores each ROM member as a masked, zlib compressed XOR delta against the arcade member of the same name.
Without the arcade ROMs a delta is only readable where the baseline is known: members with no source, and bytes past
the end of the source, whose baseline is 0xFF. Everything else is reported as changed extents.

Results are derived from the user's game data and must not be committed.
"""
import argparse
import hashlib
import json
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_KIT = ROOT / "game/references/usf2-backport-rc5-kit"
DEFAULT_OUT = ROOT / "out/evil-ryu-analysis/cps2"
DEFAULT_RESOURCE = ROOT / "out/evil-ryu-analysis/usf2/arc_us/mess/mess_resident.arc"
EXTENT_GAP = 16


def source_key(resource):
    return hashlib.shake_256(b"USF2 source key v1\0" + resource).digest(32)


def unmask(data, key, identity):
    stream = hashlib.shake_256(b"USF2 payload v1\0" + key + identity.encode()).digest(len(data))
    return bytes(a ^ b for a, b in zip(data, stream))


def decode_delta(wrapped, record, key):
    if hashlib.sha256(wrapped).hexdigest() != record["sha256"]:
        raise ValueError("payload identity differs")
    compressed = unmask(wrapped, key, record["identity"])
    if hashlib.sha256(compressed).hexdigest() != record["decoded_sha256"]:
        raise ValueError("this dump does not unlock the payload")
    return zlib.decompress(compressed)


def changed_extents(delta, gap=EXTENT_GAP):
    extents = []
    start = end = None
    for at, value in enumerate(delta):
        if value == 0:
            continue
        if start is None:
            start = end = at
        elif at - end > gap:
            extents.append((start, end + 1))
            start = at
        end = at
    if start is not None:
        extents.append((start, end + 1))
    return extents


def classify(name):
    stem, _, suffix = name.partition(".")
    number = int("".join(c for c in suffix if c.isdigit()) or "0")
    if name.endswith(".key"):
        return "key"
    if name.startswith("dl-") or suffix in ("11m", "12m"):
        return "audio"
    if number in (1, 2):
        return "z80"
    if 3 <= number <= 10:
        return "program"
    return "graphics"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kit", type=Path, default=DEFAULT_KIT)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--resource", type=Path, default=DEFAULT_RESOURCE, help="the dump's arc_us/mess/mess_resident.arc")
    args = parser.parse_args()

    kit_manifest = json.loads((args.kit / "kit.json").read_text())
    key = source_key(args.resource.read_bytes())

    args.out.mkdir(parents=True, exist_ok=True)
    members_dir = args.out / "members"
    members_dir.mkdir(exist_ok=True)
    manifest = {"format": kit_manifest.get("format"), "profiles": {}}
    lines = ["USF2 CPS-2 backport patch manifest", "format: " + str(kit_manifest.get("format")), ""]
    for profile, record in sorted(kit_manifest["profiles"].items()):
        rows = {}
        for name, member in sorted(record["members"].items()):
            payload = member.get("payload")
            row = {"size": member["size"], "source": member.get("source"), "class": classify(name),
                   "post_patch": len(member.get("post_patch", []))}
            if payload:
                wrapped = (args.kit / payload["file"]).read_bytes()
                try:
                    delta = decode_delta(wrapped, payload, key)
                except ValueError as error:
                    row["error"] = str(error)
                    rows[name] = row
                    continue
                extents = changed_extents(delta)
                row.update(file=payload["file"], changed_bytes=sum(1 for v in delta if v), extents=len(extents),
                           first_extents=[[hex(a), hex(b)] for a, b in extents[:64]])
                if member.get("source") is None:
                    (members_dir / (profile + "_" + name)).write_bytes(bytes(v ^ 0xFF for v in delta))
                else:
                    (members_dir / (profile + "_" + name + ".delta")).write_bytes(delta)
            rows[name] = row
        manifest["profiles"][profile] = rows
        lines.append("== " + profile)
        for name, row in rows.items():
            lines.append("  %-16s %9d %-9s source=%-14s changed=%-9s extents=%s" % (
                name, row["size"], row["class"], row["source"], row.get("changed_bytes", "-"), row.get("extents", row.get("error", "-"))))
    (args.out / "patch_manifest.json").write_text(json.dumps(manifest, indent=1))
    (args.out / "patch_manifest.txt").write_text("\n".join(lines) + "\n")
    print("wrote", args.out / "patch_manifest.json")


if __name__ == "__main__":
    main()
