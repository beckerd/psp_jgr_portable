#!/usr/bin/env python3
"""Audit the decompiled mission-set vehicle/prop banks against the DC tables.

Run from the repository root.  The checks cover source model/texture-table
identity, every mission-used type, JGS object/batch/index ranges, and finite
geometry.  The bank convention is N near objects followed by N far objects.
"""
import glob, json, math, os, struct, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE_PATH = os.path.join(ROOT, "decompile", "extracted", "1ST_READ.BIN")
EXE_BASE = 0x8c010000
IMG_BASE = 0x8cb00000

# stage: data, texture map, type count, near table/root, far table/root,
# texture-list table/root.  Kogane uses one fixed model pair for every record.
SPECS = {
    1: ("STAGE1/000_st1dat.bin", "stage1_texmap_static.json", 18,
        0x8c107860, 0x8c1078a8, 0x8c1078f0, False),
    2: ("STAGE2/000_st2dat.bin", "stage2_texmap.json", 8,
        0x8cc91d70, 0x8cc93010, 0x8c1ad900, True),
    3: ("STAGE3/000_st3dat.bin", "stage3_texmap.json", 13,
        0x8c1bc7e4, 0x8c1bc818, 0x8c1c7428, False),
    5: ("STAGE5/000_st5dat.bin", "stage5_texmap.json", 17,
        0x8c2042a8, 0x8c2042ec, 0x8c20a7c8, False),
    6: ("STAGE6/000_st6dat.bin", "stage6_texmap.json", 17,
        0x8c20b7f4, 0x8c20b838, 0x8c219ee0, False),
}

def words(buf, addr, count):
    return list(struct.unpack_from("<%dI" % count, buf, addr - EXE_BASE))

def mission_sets():
    d = open(os.path.join(ROOT, "psp", "missions.bin"), "rb").read()
    assert d[:4] == b"MIS3"
    n = struct.unpack_from("<I", d, 4)[0]; out = {}
    stage_of_index = {0: 1, 1: 2, 2: 3, 4: 5, 5: 6}
    for i in range(n):
        st, _, sn = struct.unpack_from("<3I", d, 8 + i * 88)
        if st in stage_of_index: out.setdefault(stage_of_index[st], set()).add(sn)
    return out

def placed_types(sets):
    counts = {}
    for sn in sets:
        p = os.path.join(ROOT, "psp", "set%d_obj.bin" % sn)
        if not os.path.exists(p): continue
        d = open(p, "rb").read(); assert d[:4] == b"SOB1"
        q = 8
        for _ in range(struct.unpack_from("<I", d, 4)[0]):
            n = struct.unpack_from("<I", d, q)[0]; q += 4
            for _ in range(n):
                ty = struct.unpack_from("<I", d, q + 4)[0]
                counts[ty] = counts.get(ty, 0) + 1; q += 24
        assert q == len(d)
    return counts

def audit_jgs(path, ntypes, expected_roots):
    with open(path, "rb") as source: d = source.read()
    magic, nt, no, nb, nv, ni = struct.unpack_from("<4s5I", d, 0)
    assert magic == b"JGS1" and no == ntypes * 2
    ot, oo, ob, ov, oi, op = struct.unpack_from("<6I", d, 24)
    assert 0 <= ot <= oo <= ob <= ov <= oi <= op <= len(d)
    with open(path + ".roots.json") as source: roots = json.load(source)
    assert [int(v, 16) for v in roots] == expected_roots
    obj_batches = []; obj_tex = []
    for i in range(no):
        cx, cy, cz, r, first, count, _, _ = struct.unpack_from("<ffffIIII", d, oo + i * 32)
        assert all(math.isfinite(v) for v in (cx, cy, cz, r)) and r >= 0 and first + count <= nb
        obj_batches.append(count); obj_tex.append(set())
    for i in range(nb):
        vo, io, nvv, nii, tex, flags = struct.unpack_from("<IIHHHH", d, ob + i * 16)
        assert vo + nvv <= nv and io + nii <= ni and nii % 3 == 0 and tex < nt
        if not (flags & 4):
            for oi0, (first, count) in enumerate(
                    struct.unpack_from("<II", d, oo + j * 32 + 16) for j in range(no)):
                if first <= i < first + count: obj_tex[oi0].add(tex); break
        if nii:
            inds = struct.unpack_from("<%dH" % nii, d, oi + io * 2)
            assert max(inds) < nvv
    for i in range(nv):
        u, v, _, x, y, z = struct.unpack_from("<ffIfff", d, ov + i * 24)
        assert all(math.isfinite(q) for q in (u, v, x, y, z))
    fallback = set()
    for i in range(nt):
        w, h, _, _, oc, op = struct.unpack_from("<HHHHII", d, ot + i * 16)
        # jgs_convert's only fabricated image is a solid 16x16 (160,160,160)
        # fallback.  It may remain in an unused placeholder slot, but no
        # mission-placed source object is allowed to reference it.
        colors = {tuple(d[j:j + 4]) for j in range(oc, oc + 1024, 4)}
        if w == h == 16 and colors <= {(160, 160, 160, 255), (0, 0, 0, 0)}:
            fallback.add(i)
    ntri = sum(struct.unpack_from("<H", d, ob + i * 16 + 10)[0] // 3 for i in range(nb))
    return nt, nb, nv, ntri, obj_batches, obj_tex, fallback

def main():
    exe = open(EXE_PATH, "rb").read(); sets = mission_sets()
    totals = [0, 0, 0, 0, 0]
    for st, (dat, tmname, ntypes, near, far, textab, fixed) in SPECS.items():
        if fixed:
            nr = [near] * ntypes; fr = [far] * ntypes; tls = [textab] * ntypes
        else:
            nr = words(exe, near, ntypes); fr = words(exe, far, ntypes); tls = words(exe, textab, ntypes)
        tm = json.load(open(os.path.join(ROOT, "decompile", tmname)))
        tlmap = {int(k): v for k, v in tm.get("texlists", {}).items()}
        used = placed_types(sets.get(st, ()))
        for i in range(ntypes):
            if tls[i]:
                assert tls[i] in tlmap and -1 not in tlmap[tls[i]], "stage %d type %d unresolved texture list" % (st, i)
            if i in used and (nr[i] or fr[i]):
                assert tls[i], "stage %d used visual type %d has no source texture list" % (st, i)
        assert all(t < ntypes for t in used), "stage %d placement type outside source table" % st
        collision_only = [t for t in sorted(used) if not nr[t] and not fr[t]]
        p = os.path.join(ROOT, "psp", "stage%d_obj.jgs" % st)
        nt, nb, nv, ntri, ob, obj_tex, fallback = audit_jgs(p, ntypes, nr + fr)
        for t in used:
            assert (ob[t] > 0 or ob[t + ntypes] > 0) == bool(nr[t] or fr[t])
            assert not ((obj_tex[t] | obj_tex[t + ntypes]) & fallback), \
                "stage %d used type %d references fabricated fallback texture" % (st, t)
        vals = (sum(used.values()), len(used) - len(collision_only), nb, ntri, nt)
        totals = [a + b for a, b in zip(totals, vals)]
        print("stage %d: %d placements, %d visible types, collision-only %s; %d batches, %d tris, %d textures" %
              (st, vals[0], vals[1], collision_only or "none", nb, ntri, nt))
    print("PASS: %d placements, %d visible source types, %d batches, %d triangles, %d bank textures" % tuple(totals))

if __name__ == "__main__": main()
