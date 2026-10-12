#!/usr/bin/env python3
"""Regenerate motion mapping and spray tables from the supported user executable.

Only addresses, dimensions, formats and output syntax are included here. No
original table contents or fallback game values are distributed by this tool.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct

# Keep the known image identity explicit; no command-line bypass is provided.
EXECUTABLE_SHA256 = "8b294765225fc32020dd59010464a730b710d0bd79f69c79f16218cc4feac6a6"
IMAGE_BASE = 0x8c010000


def extract(image: bytes, *, expected_sha256: str = EXECUTABLE_SHA256) -> dict[str, bytes]:
    if hashlib.sha256(image).hexdigest() != expected_sha256:
        raise ValueError("Unsupported executable identity; tables require the reviewed US disc executable")

    def words(address, count):
        offset = address - IMAGE_BASE
        if offset < 0 or count < 0 or offset + count * 4 > len(image):
            raise ValueError("Executable table is outside the supplied image")
        return struct.unpack_from("<" + "i" * count, image, offset)

    motion = [words(0x8c10a808 + i * 16, 2) for i in range(112)]
    if any(not -1 <= x <= 32767 for row in motion for x in row):
        raise ValueError("Motion mapping cannot be represented by the runtime short ABI")
    motion_text = ["/* motion id -> PLAY animation index (table 0x8c10a808, decompiled; -1 = none). alt = low-health variant */",
                   "#define NMOTMAP 112"]
    for column, name in enumerate(("MOTMAP", "MOTMAP_ALT")):
        motion_text.append(f"static const short {name}[NMOTMAP] = {{" + ",".join(str(row[column]) for row in motion) + "};")
    cues = [words(0x8c17e3f0 + i * 28, 7) for i in range(11)]
    patterns = [words(0x8c17e524 + i * 16, 4) for i in range(21)]
    if any(not 1 <= row[3] <= 3 or any(not 0 <= x < 11 for x in row[:3]) for row in patterns):
        raise ValueError("Spray pattern references an invalid cue")
    spray_text = ["#ifndef JGR_SPRAYTAB_H", "#define JGR_SPRAYTAB_H",
                  "/* Spray minigame tables from the DC executable (0x8c17e3f0 cue descriptors, 0x8c17e524 pattern records).",
                  "   cue: {a0, a1, mid, x, end, score, kind}; record: {id0, id1, id2, count}; blocks of 7 records per CHARACTER graffiti skill. */"]
    for name, macro, rows, width in (("CUES", "N_CUES", cues, 7), ("PATROWS", "N_PATROWS", patterns, 4)):
        spray_text.append(f"#define {macro} {len(rows)}")
        spray_text.append(f"static const int {name}[{macro}][{width}] = {{" +
                          ",".join("{" + ",".join(map(str, row)) + "}" for row in rows) + "};")
    spray_text.append("#endif")
    return {"motmap.h": ("\n".join(motion_text) + "\n").encode(),
            "spraytab.h": ("\n".join(spray_text) + "\n").encode()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="Existing generated source directory")
    args = parser.parse_args()
    try:
        generated = extract(args.executable.read_bytes())
        args.out.mkdir(parents=True, exist_ok=True)
        for name, data in generated.items():
            (args.out / name).write_bytes(data)
        print("Generated motmap.h and spraytab.h from the verified user executable")
    except (OSError, ValueError, struct.error) as exc:
        parser.exit(1, f"source-data-tables: {exc}\n")


if __name__ == "__main__":
    main()
