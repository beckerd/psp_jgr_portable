#!/usr/bin/env python3
"""Build explicit JGC conversion specs for the four Dreamcast street-person banks.

The ENEMY people files are raw 0x8cad0000 images.  Each regular person is an
11-node Ninja object and owns the same-numbered one-entry texture list/PVR.
The shared motion tables contain two three-node reactions used only by the
special 0x2b/0x2c actors; those cannot be evaluated on an 11-node person.
This emits the 31 source motions reachable by regular pedestrians while
keeping the original table indices documented below.
"""
import struct
from pathlib import Path

BASE = 0x8CAD0000
HERE = Path(__file__).resolve().parent.parent
BANKS = {
    1: ("047_people1dat.bin", "046_people1txr.bin", 21),
    2: ("049_people2dat.bin", "048_people2txr.bin", 18),
    3: ("051_people3dat.bin", "050_people3txr.bin", 23),
    5: ("053_people5dat.bin", "052_people5txr.bin", 17),
}

# The pedestrian type namespace is global.  Kogane and Benten mission sets
# still use a handful of the people1 models, so their runtime loader tables
# retain those earlier entries instead of remapping them to a local model.
# Keep the level banks self-contained on PSP by appending those exact roots.
INHERITED = {
    2: ((1, 0), (1, 1), (1, 4), (1, 5), (1, 6)),
    3: ((1, 3), (1, 4), (1, 6), (1, 12)),
}

# people1 also owns the two untextured three-node actors selected by global
# types 0x2b/0x2c.  Their only compatible motions are normal[22] and
# special[4]; treating them as the 12-node ENEMY police model was wrong.
SPECIAL = {
    1: ((43, 0), (44, 1)),
    2: ((44, 1),),
}


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def in_image(value, size, alignment=4):
    return BASE <= value < BASE + size and value % alignment == 0


def objects(data):
    out = {}
    for offset in range(0, len(data) - 0x34, 4):
        flags = u32(data, offset)
        if flags > 0xffff:
            continue
        model, child, sibling = u32(data, offset + 4), u32(data, offset + 0x2c), u32(data, offset + 0x30)
        if not (model or child or sibling):
            continue
        if not all(value == 0 or in_image(value, len(data)) for value in (model, child, sibling)):
            continue
        scale = struct.unpack_from("<3f", data, offset + 0x20)
        if all(1e-4 < value < 1e3 for value in scale):
            out[BASE + offset] = (child, sibling)
    return out


def tree_size(root, obj):
    seen, todo = set(), [root]
    while todo:
        address = todo.pop()
        if not address or address not in obj or address in seen:
            continue
        seen.add(address)
        todo.extend(obj[address])
    return len(seen)


def motion_headers(data):
    """Return the 35 unique headers in address/table order.

    Validating three channels intentionally admits normal[22] and special[4],
    the two motions belonging to the three-node special actors.  They are
    then omitted explicitly when constructing a regular-person bank.
    """
    found = []
    for offset in range(0, len(data) - 7, 4):
        header, channels = u32(data, offset), u32(data, offset + 4)
        if (header & 0xffff) != 0x11 or not (1 <= header >> 16 < 0x200):
            continue
        if not in_image(channels, len(data)):
            continue
        valid = True
        for bone in range(3):
            record = channels - BASE + bone * 16
            if record + 16 > len(data):
                valid = False
                break
            flags, pos, ang, scale = struct.unpack_from("<4I", data, record)
            if flags > 0x17 or not (pos == 0 or in_image(pos, len(data))) or not (ang == 0 or in_image(ang, len(data), 2)) or not (scale == 0 or in_image(scale, len(data))):
                valid = False
                break
        if valid:
            found.append(BASE + offset)
    return found


bank_info = {}
for stage, (dat_name, txr_name, expected_models) in BANKS.items():
    dat_path = HERE / "afs" / "ENEMY" / dat_name
    txr_path = HERE / "afs" / "ENEMY" / txr_name
    data, txr = dat_path.read_bytes(), txr_path.read_bytes()
    obj = objects(data)
    children = {value for pair in obj.values() for value in pair if value}
    all_roots = sorted(address for address in obj if address not in children)
    roots = [address for address in all_roots if tree_size(address, obj) == 11]
    special_roots = [address for address in all_roots if tree_size(address, obj) == 3]
    motions = motion_headers(data)
    pvr_count = txr.count(b"GBIX")
    if len(roots) != expected_models or pvr_count != expected_models:
        raise ValueError(f"people{stage}: {len(roots)} roots / {pvr_count} PVRs, expected {expected_models}")
    if len(motions) != 35:
        raise ValueError(f"people{stage}: found {len(motions)} unique motion headers, expected 35")

    # Source normal table 0..26, special table 0..8.  normal[22] and
    # special[4] have three channels; normal[25:27] are not reachable by a
    # route byte (< 25) or the regular-person state handlers.
    chosen = motions[0:22] + motions[23:25] + motions[27:30] + motions[31:35]
    motion_arg = ",".join(f"0x{address:08x}" for address in chosen)
    rows = []
    for index, root in enumerate(roots):
        rows.append(
            f"P{stage}_{index:02d}:afs/ENEMY/{dat_name}:afs/ENEMY/{txr_name}:0,0,0,0:"
            f"rawat:0x{BASE:08x}:0x{root:08x}:{motion_arg}:mot={len(chosen)}:tex={index}"
        )
    bank_info[stage] = (dat_name, txr_name, roots, special_roots, motions, motion_arg)
    output = HERE / f"people{stage}_spec.txt"
    output.write_text("\n".join(rows) + "\n")
    print(f"{output.name}: {len(roots)} local models, {len(chosen)} reachable source motions each, {pvr_count} textures")

for target in BANKS:
    output = HERE / f"people{target}_spec.txt"
    rows = output.read_text().splitlines()
    for source, index in INHERITED.get(target, ()):
        dat_name, txr_name, roots, _, _, motion_arg = bank_info[source]
        rows.append(
            f"P{source}_{index:02d}_shared:afs/ENEMY/{dat_name}:afs/ENEMY/{txr_name}:0,0,0,0:"
            f"rawat:0x{BASE:08x}:0x{roots[index]:08x}:{motion_arg}:mot=31:tex={index}"
        )
    for type_id, special_index in SPECIAL.get(target, ()):
        dat_name, txr_name, _, special_roots, motions, _ = bank_info[1]
        special_motion_arg = f"0x{motions[22]:08x},0x{motions[31]:08x}"
        rows.append(
            f"P1_special_{type_id}:afs/ENEMY/{dat_name}:afs/ENEMY/{txr_name}:0,0,0,0:"
            f"rawat:0x{BASE:08x}:0x{special_roots[special_index]:08x}:{special_motion_arg}:mot=2"
        )
    output.write_text("\n".join(rows) + "\n")
    print(f"{output.name}: {len(rows)} total PSP models ({len(INHERITED.get(target, ()))} shared, {len(SPECIAL.get(target, ()))} special)")
