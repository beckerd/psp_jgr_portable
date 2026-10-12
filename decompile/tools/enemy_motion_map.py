#!/usr/bin/env python3
"""Map Jet Grind Radio's semantic ENEMY animation table to raw-bank motions.

The enemy initializers select one of the executable's semantic pointer tables
for the data image resident at 0x8ca00000.  jgc_convert.py stores valid raw
motion headers in ascending file-offset order; this tool reports (and can emit
C for) the corresponding local JGC indices.
"""
from __future__ import annotations

import argparse
import pathlib
import struct


EXE_BASE = 0x8C010000
ENEMY_BASE = 0x8CA00000
MOTION_TABLE = 0x8C1B5AF4

# Loader, source table, copied entries, post-copy aliases.  The aliases are
# literal assignments in the two loader functions, not inferred clips:
#   FUN_8c076194: runtime[9]  = runtime[32] (keikan)
#   FUN_8c07947c: runtime[11] = runtime[9]  (kidoutai)
KNOWN_TABLES = {
    "keikan": (0x8C1B5AF4, 37, {9: 32}, 0),
    "kidoutai": (0x8C1B5BA0, 36, {11: 9}, 0),
    # FUN_8c0948e0/FUN_8c07b6b0/FUN_8c08d1b8/FUN_8c07da6c
    # are the private loaders for Dog, Bomb, Kairiki and Fire.  Each walks
    # its own zero-terminated semantic pointer table; these are not PLAY
    # actors and must never be indexed through the 176-entry skater table.
    "inu": (0x8C1B5C74, 14, {}, 0),
    "killer": (0x8C1B5CC8, 33, {}, 0),
    "bomb": (0x8C1B5D64, 26, {}, 0),
    "kairiki": (0x8C1B5DF8, 29, {}, 0),
    "para": (0x8C1B5E8C, 35, {}, 0),
    # FUN_8c09995c relocates this table for all four 12-node character roots
    # in 015_rocketdat.bin.  Creator FUN_8c0997b4 selects roots 0/1 for
    # group type 24 and roots 2/3 for type 8.
    "rocket": (0x8C1B5F54, 27, {}, 0),
    "fire": (0x8C1B5FDC, 35, {}, 0),
    # FUN_8c0a16f4 relocates the zero-terminated 20-entry table against the
    # ten-node detailed Tank root used by group type 10.  The creator is
    # FUN_8c0a135c; its alternate creator FUN_8c0a1560 uses the same bank.
    "tank": (0x8C1B6160, 20, {}, 0),
    # These tables identify the actual actors created by group types 11/12:
    # FUN_8c07f840 relocates 0x8c1b620c against PatCar's 8-bone image and
    # FUN_8c083200 relocates 0x8c1b62bc against Bike's 14-bone image.
    "patcar": (0x8C1B620C, 16, {}, 0),
    "bike": (0x8C1B62BC, 16, {}, 0),
    # FUN_8c0acb48 relocates this 15-entry table for the 7-bone Benz bank.
    "benz": (0x8C1B6408, 15, {}, 0),
    # FUN_8c09bf24 relocates all 13 entries against Sniper's two 12-bone
    # near/far roots.  FUN_8c0fd120 does the same for Denki's five primary
    # character motions (the remaining Denki roots are effect objects).
    "sniper": (0x8C1B60A0, 13, {}, 0),
    # FUN_8c08594c relocates the zero-terminated 24-entry table for group
    # type 17 against 033_onisimadat.bin.  Type 21 has its own Taban loader.
    "onisima": (0x8C1B60EC, 24, {}, 0),
    "denki": (0x8C1B665C, 5, {}, 0),
    # FUN_8c0f9308 relocates all 31 entries for 035_tabandat.bin.
    "taban": (0x8C1B65CC, 31, {}, 0),
    # FUN_8c0df420/FUN_8c0df4a4/FUN_8c0df528 are the three private loaders
    # selected by creator FUN_8c0df39c for group types 18/19/20.
    "rival1": (0x8C1B659C, 3, {}, 0),
    "rival2": (0x8C1B65A8, 3, {}, 0),
    "rival3": (0x8C1B65B4, 3, {}, 0),
    # FUN_8c0a5ab0/FUN_8c0a5bf4 use the 4-object vehicle root (root index 4),
    # rather than the largest 9-object effects subtree.  Keep bheri before
    # heri because its filename also contains the latter substring.
    "bheri": (0x8C1B6498, 7, {}, 4),
    "heri": (0x8C1B6348, 7, {}, 4),
}


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def object_roots(data: bytes, base: int) -> list[tuple[int, int]]:
    """Return candidate NJS_OBJECT roots as (subtree size, address)."""
    size = len(data)

    def is_ptr(value: int, alignment: int = 4) -> bool:
        return base <= value < base + size and value % alignment == 0

    objects: dict[int, tuple[int, int]] = {}
    for offset in range(0, size - 0x34, 4):
        eval_flags = u32(data, offset)
        if eval_flags > 0xFFFF:
            continue
        model, child, sibling = (u32(data, offset + n) for n in (4, 0x2C, 0x30))
        if not all(value == 0 or is_ptr(value) for value in (model, child, sibling)):
            continue
        if model == child == sibling == 0:
            continue
        scale = struct.unpack_from("<3f", data, offset + 0x20)
        if not all(1e-4 < value < 1e3 for value in scale):
            continue
        objects[base + offset] = child, sibling

    children = {value for pair in objects.values() for value in pair if value}

    def subtree_size(root: int) -> int:
        count, stack, seen = 0, [root], set()
        while stack:
            address = stack.pop()
            if not address or address not in objects or address in seen:
                continue
            seen.add(address)
            count += 1
            stack.extend(objects[address])
        return count

    roots = [(subtree_size(address), address) for address in objects if address not in children]
    return sorted(roots, key=lambda item: (-item[0], item[1]))


def motion_headers(data: bytes, node_count: int, base: int) -> list[int]:
    """Use the same raw NJS_MOTION validation as jgc_convert.py."""
    size = len(data)

    def is_ptr(value: int, alignment: int = 4) -> bool:
        return base <= value < base + size and value % alignment == 0

    result = []
    for offset in range(0, size - 4, 4):
        word, mdata = struct.unpack_from("<II", data, offset)
        if not (0x10000 <= word < 0x1000000 and 0 < (word & 0xFFFF) < 0x200 and is_ptr(mdata)):
            continue
        record = mdata - base
        valid = True
        for bone in range(node_count):
            pos = record + bone * 16
            if pos + 16 > size:
                valid = False
                break
            key_count, p_pos, p_ang, p_scale = struct.unpack_from("<4I", data, pos)
            if key_count >= 0x200 or not (p_pos == 0 or is_ptr(p_pos)) or not (p_ang == 0 or is_ptr(p_ang, 2)) or not (p_scale == 0 or is_ptr(p_scale)):
                valid = False
                break
        if valid:
            result.append(offset)
    return result


def source_slots(exe: bytes, address: int = MOTION_TABLE, count: int | None = None) -> list[int]:
    result = []
    offset = address - EXE_BASE
    while True:
        pointer = u32(exe, offset + len(result) * 4)
        if count is None and pointer == 0:
            return result
        result.append(pointer - ENEMY_BASE if ENEMY_BASE <= pointer < 0x8CB00000 else -1)
        if count is not None and len(result) == count:
            return result


def identifier(path: pathlib.Path) -> str:
    name = path.stem
    if "_" in name:
        name = name.split("_", 1)[1]
    if name.endswith("dat"):
        name = name[:-3]
    return "enemy_motion_" + "".join(c if c.isalnum() else "_" for c in name.lower())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=pathlib.Path)
    parser.add_argument("banks", nargs="+", type=pathlib.Path)
    parser.add_argument("--c", action="store_true", help="emit static C lookup arrays")
    args = parser.parse_args()

    exe = args.exe.read_bytes()

    for path in args.banks:
        key = next((name for name in KNOWN_TABLES if name in path.name.lower()), None)
        if key:
            table_address, table_count, aliases, root_index = KNOWN_TABLES[key]
            slots = source_slots(exe, table_address, table_count)
            for destination, source in aliases.items():
                slots[destination] = slots[source]
        else:
            table_address, aliases, root_index = MOTION_TABLE, {}, 0
            slots = source_slots(exe)
        data = path.read_bytes()
        roots = object_roots(data, ENEMY_BASE)
        if not roots:
            raise SystemExit(f"{path}: no NJS_OBJECT root")
        if root_index >= len(roots):
            raise SystemExit(f"{path}: requested root {root_index}, only {len(roots)} found")
        node_count, root = roots[root_index]
        headers = motion_headers(data, node_count, ENEMY_BASE)
        index = {offset: i for i, offset in enumerate(headers)}
        mapping = [index.get(offset, -1) for offset in slots]
        if args.c:
            values = ", ".join(str(value) for value in mapping)
            print(f"static const signed char {identifier(path)}[{len(mapping)}] = {{ {values} }};")
        else:
            alias_text = " ".join(f"{dst}<-{src}" for dst, src in aliases.items()) or "none"
            print(f"{path.name}: root={root:#010x} bones={node_count} motions={len(headers)} table={table_address:#010x} aliases={alias_text}")
            print("  source slots: " + " ".join(f"{i}:{value}" for i, value in enumerate(mapping)))


if __name__ == "__main__":
    main()
