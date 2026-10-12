#!/usr/bin/env python3
"""Bake a JSR stage memory image into a JGS1 file for the PSP viewer.
usage: jgs_convert.py STAGE.bin BASE OUT.jgs [TEXPACK...] [--layout=L.json] [--texmap=T.json] [--texmax=128] [--nolight]
- layout: instances from the RE'd placement tables (proto tables + records); objects not in any proto table are drawn once
  --stage-profile=N preserves the shipping mixed placement conventions; stage_render_assets retires duplicate panorama placeholders.
  in world space; far-LOD / handler prototypes ('exclude') are skipped; 'model_inst' = raw NJS_CNK_MODEL instances (group structs)
- texmap: per-object texture slot -> texpack index (from RAM dumps); unresolved slots -> flat grey"""
import collections
import struct, sys, os, math, json
sys.path.insert(0, os.path.dirname(__file__))
from ninjalib import *
from pvr_decode import decode as pvr_decode
args = [a for a in sys.argv[1:] if not a.startswith("--")]; opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--"))
stage_path, B, out_path, texpacks = args[0], int(args[1], 16), args[2], args[3:]
TEXMAX = int(opts.get("texmax", 128)); LIGHT = "nolight" not in opts
LAYOUT = json.load(open(opts["layout"])) if "layout" in opts else None
import ninjalib as _nl; _nl.MAXEDGE = float(opts.get("maxedge", 0))
if 'baked-light' in opts:
    from baked_light import PROFILES
    _nl.BAKED_LIGHT = opts['baked-light']; assert _nl.BAKED_LIGHT in PROFILES
TEXMAP = None; TEXLISTS = None
if "texmap" in opts:
    tm = json.load(open(opts["texmap"])); texpacks = tm["packs"]; TEXMAP = {int(k): v for k, v in tm["texmap"].items()}
    TEXLISTS = {int(k): v for k, v in tm.get("texlists", {}).items()}
d = open(stage_path, "rb").read(); N = len(d)
u32 = lambda o: struct.unpack_from("<I", d, o)[0]; f32 = lambda o: struct.unpack_from("<f", d, o)[0]
isp = lambda v: B <= v < B + N and (v & 3) == 0; isp0 = lambda v: v == 0 or isp(v)
# ---- objects ----
objs = {}
for o in range(0, N - 0x34, 4):
    ev = u32(o)
    if ev > 0xffff: continue
    model, child, sib = u32(o + 4), u32(o + 0x2c), u32(o + 0x30)
    if not (isp0(model) and isp0(child) and isp0(sib)) or (model == 0 and child == 0 and sib == 0): continue
    if not all(-1e6 < f32(o + 8 + 4 * i) < 1e6 for i in range(3)) or not all(1e-4 < f32(o + 0x20 + 4 * i) < 1e3 for i in range(3)): continue
    objs[B + o] = True
children = set()
for a in objs:
    c, s = struct.unpack_from("<II", d, a - B + 0x2c); children.add(c); children.add(s)
roots = sorted(a for a in objs if a not in children)
# Graffiti templates own an NJS_OBJECT at +0xfc.  These are not static world
# geometry: the graffiti task draws them with a run-time-selected tag texture.
# Treating them as ordinary roots baked an unbound texlist into every level
# (90 grey/white plaques in Kogane alone) and also duplicated the PSP's spot
# marker/decal path.  The record signature is unambiguous in all five stage
# images: size 1..3 followed by a valid root pointer at +0xfc.
SPOTROOTS = set()
for o in range(0, N - 0x100, 4):
    if u32(o) in (1, 2, 3) and u32(o + 0xfc) in objs:
        SPOTROOTS.add(u32(o + 0xfc) - B)
print(f"graffiti-owned roots: {len(SPOTROOTS)}")
if "roottable" in opts:
    # Reproducible extraction of an executable-resident NJS_OBJECT* table.
    # This is used by the stage-specific set-object banks (parked cars,
    # buses, barriers, etc.); zero entries remain object-index placeholders.
    _exe_path = opts.get("exe", os.path.join(os.path.dirname(__file__), "..", "extracted", "1ST_READ.BIN"))
    _exe = open(_exe_path, "rb").read(); _eb = int(opts.get("exebase", "0x8c010000"), 0)
    _ta = int(opts["roottable"], 0); _tn = int(opts["rootcount"])
    roots = list(struct.unpack_from("<%dI" % _tn, _exe, _ta - _eb))
if "roots" in opts:   # explicit root list: one object per root, in order, at identity
    roots = [int(x, 16) for x in opts["roots"].split(",")]
if "appendroottable" in opts:
    _exe_path = opts.get("exe", os.path.join(os.path.dirname(__file__), "..", "extracted", "1ST_READ.BIN"))
    _exe = open(_exe_path, "rb").read(); _eb = int(opts.get("exebase", "0x8c010000"), 0)
    _ta = int(opts["appendroottable"], 0); _tn = int(opts["rootcount"])
    roots.extend(struct.unpack_from("<%dI" % _tn, _exe, _ta - _eb))
print(f"objects {len(objs)} roots {len(roots)}")
json.dump([hex(r) for r in roots], open(out_path + ".roots.json", "w"))   # object index -> root address
# ---- textures ----
tex_imgs = []; tex_gbix = []
for tp in texpacks:
    b = open(tp, "rb").read(); i = 0
    while True:
        i = b.find(b"GBIX", i)
        if i < 0: break
        tex_gbix.append(struct.unpack_from("<I", b, i + 8)[0])
        j = b.find(b"PVRT", i + 8, i + 64)
        try: L = struct.unpack_from("<I", b, j + 4)[0]; tex_imgs.append(pvr_decode(b[j:j + 8 + L]))
        except Exception: tex_imgs.append(Image.new("RGBA", (8, 8), (255, 0, 255, 255)))
        i += 8
tex_imgs.append(Image.new("RGBA", (16, 16), (160, 160, 160, 255))); FALLBACK = len(tex_imgs) - 1
print(f"textures decoded: {len(tex_imgs) - 1} (+fallback)")
ROOTGBIX = [int(x) for x in opts["rootgbix"].split(",")] if "rootgbix" in opts else None   # per listed root: GBIX of texture slot 0 (set-object texlists, RAM dump)
ROOTTEXLISTS = [int(x, 0) for x in opts["roottexlists"].split(",")] if "roottexlists" in opts else None
FALLBACK_USE = {}
if "roottextable" in opts:
    _exe_path = opts.get("exe", os.path.join(os.path.dirname(__file__), "..", "extracted", "1ST_READ.BIN"))
    _exe = open(_exe_path, "rb").read(); _eb = int(opts.get("exebase", "0x8c010000"), 0)
    _ta = int(opts["roottextable"], 0); _tn = int(opts["rootcount"])
    ROOTTEXLISTS = list(struct.unpack_from("<%dI" % _tn, _exe, _ta - _eb))
if "appendroottextable" in opts:
    _exe_path = opts.get("exe", os.path.join(os.path.dirname(__file__), "..", "extracted", "1ST_READ.BIN"))
    _exe = open(_exe_path, "rb").read(); _eb = int(opts.get("exebase", "0x8c010000"), 0)
    _ta = int(opts["appendroottextable"], 0); _tn = int(opts["rootcount"])
    if ROOTTEXLISTS is None: ROOTTEXLISTS = []
    ROOTTEXLISTS.extend(struct.unpack_from("<%dI" % _tn, _exe, _ta - _eb))
def texmap_for(objoff, ordinal):
    if ROOTTEXLISTS is not None:
        a = ROOTTEXLISTS[ordinal] if ordinal < len(ROOTTEXLISTS) else 0
        lst = TEXLISTS.get(a) if TEXLISTS is not None else None
        return lambda s: (lst[s] if lst and s < len(lst) and lst[s] >= 0 else FALLBACK)
    if ROOTGBIX is not None:
        g = ROOTGBIX[ordinal] if ordinal < len(ROOTGBIX) else -1; idx = tex_gbix.index(g) if g in tex_gbix else FALLBACK
        return lambda s: (idx if s == 0 else FALLBACK)
    if "identmap" in opts: return lambda s: (s if s < len(tex_imgs) - 1 else FALLBACK)   # slot = index into the given texpack(s)
    if TEXMAP is not None:
        lst = TEXMAP.get(objoff)
        def mapped(s):
            if lst and s < len(lst) and lst[s] >= 0: return lst[s]
            FALLBACK_USE[(objoff, s)] = FALLBACK_USE.get((objoff, s), 0) + 1
            return FALLBACK
        return mapped
    return lambda s: (ordinal * 13 + s) % (len(tex_imgs) - 1)
# ---- placement ----
INST = {}; PROFILES = {}; PROTOSET = set(); EXCLUDE = set(); MODEL_INST = []; FTILES = set()
if LAYOUT is not None:
    PROTOSET = set(LAYOUT["protoset"]); EXCLUDE = set(LAYOUT.get("exclude", []))
    if 'stage-profile' in opts:
        from stage_render_assets import Source, TABLES, SECTORS, key, placement_geometry, placement_light_transform
        profile_stage = int(opts['stage-profile']); profile_source = Source(profile_stage)
        profile_records = collections.defaultdict(collections.deque)
        for tag,(table,near,far,stride,count,fartex) in TABLES[profile_stage].items():
            for record in profile_source.records(table,SECTORS[profile_stage][0],stride,count):
                profile_records[key(tag,record['pid'],record['pos'],record['ang'])].append(record)
    for tag, pid, pos, ang in LAYOUT["inst"]:
        objoff = LAYOUT["proto"][tag][pid]
        if objoff < 0: continue
        INST.setdefault(objoff, []).append((pos, ang))
        if 'stage-profile' in opts:
            queue = profile_records.get(key(tag,pid,pos,ang))
            record = queue.popleft() if queue else None
            if tag in TABLES[profile_stage] and record is None:
                raise ValueError('Missing source placement for stage profile')
            matrix,post=placement_geometry(profile_stage,tag,pid,pos,ang,record)
            lm=placement_light_transform(profile_stage,tag,pid,pos,ang,record,matrix)
            PROFILES.setdefault(objoff, []).append((matrix,post,lm))
    MODEL_INST = LAYOUT.get("model_inst", [])
    FTILES = set(LAYOUT["proto"]["F"]) if "F" in LAYOUT["proto"] else set()   # category F ground tiles are wound the other way: bake them two-sided
# every stage-image pointer listed in an exe array of 8+ entries is a prototype of some placement system (item / prop / group /
# far-LOD tables, many not modelled here): never draw those at identity. The world-space roots (sky domes) sit in 2- and 7-entry lists.
PROTOARR = set()
if LAYOUT is not None:
    _exe = open(opts.get("exe", os.path.join(os.path.dirname(__file__), "..", "extracted", "1ST_READ.BIN")), "rb").read()
    _W = struct.unpack_from("<%dI" % (len(_exe) // 4), _exe); _i = 0; _N = len(d)
    while _i < len(_W):
        if B <= _W[_i] < B + _N and (_W[_i] & 3) == 0:
            _j = _i
            while _j < len(_W) and B <= _W[_j] < B + _N and (_W[_j] & 3) == 0: _j += 1
            if _j - _i >= 8: PROTOARR.update(_W[_k] - B for _k in range(_i, _j))
            _i = _j
        else: _i += 1
    print(f"prototype-array offsets: {len(PROTOARR)}")
    if os.path.basename(opts.get("layout", "")) == "stage1_layout.json" and opts.get("stage-profile") != "1":
        # 031110's private panorama table has only TWO roots, below the
        # generic array scan's threshold. Its 105e58 placement records are
        # handled by stage_render_assets.py; never bake it at identity.
        PROTOARR.update(a-B for a in struct.unpack_from('<2I',_exe,0x8c106e04-0x8c010000) if B<=a<B+_N)
bld = JgsBuilder(); ordinal = 0; skipped = 0; SIDE = []   # per emitted object: [source offset, kind, n placements]
for r in roots:
    off = r - B
    if LAYOUT is not None and "roots" not in opts and "roottable" not in opts and off in SPOTROOTS:
        skipped += 1; continue
    if LAYOUT is not None and off in EXCLUDE and off not in PROTOSET: skipped += 1; continue
    if LAYOUT is not None and off not in PROTOSET and "roots" not in opts and off in PROTOARR: skipped += 1; continue
    if LAYOUT is not None and off not in PROTOSET and "roots" not in opts:
        # No-UV standalone roots are not automatically static architecture
        # (e.g. Bantam's 438-node collision/shadow root at 0xa7dfc). Some ARE
        # visible vertex-colour sky domes: city_sky_assets.py exports the
        # exact 0c34e8-owned roots separately, including those with no texlist.
        # Do not relax this architecture filter to recover the sky.
        has_uv = False; has_static_texlist = False
        for node, m in walk_tree(d, r, B, [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1], valid=objs, root_sib=True):
            noff = node["addr"] - B
            if TEXMAP is not None and noff in TEXMAP and any(t >= 0 for t in TEXMAP[noff]):
                has_static_texlist = True
            if node["model"]:
                pl = struct.unpack_from("<I", d, node["model"] - B + 4)[0]
                if pl and any(t[0] != 0xffff for t in parse_plist(d, pl - B)): has_uv = True
        if not has_uv: skipped += 1; continue
        # Static architecture is represented in the executable's
        # object->texlist tables.  UV-bearing roots absent from those tables
        # are models owned by another run-time system (actors, effects,
        # graffiti, etc.), not extra world geometry.  Baking them at identity
        # was especially destructive in Grind Square, where whole animated
        # model banks appeared at the origin with the grey fallback texture.
        if TEXMAP is not None and not has_static_texlist: skipped += 1; continue
    if LAYOUT is not None and off in PROTOSET:
        placements = INST.get(off, [])
        # a prototype with no placement record is drawn once at identity (world-space authored piece)
        if not placements: skipped += 1; continue   # a prototype nobody places is never drawn (Kogane: dozens piled at the origin)
        bases = PROFILES.get(off, [(placement_matrix(p, a),None,None) for p, a in placements])
    else:
        bases = [([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1],None,None)]
    root_tmap = texmap_for(off, ordinal)
    nobj0 = len(bld.objs); nb0 = len(bld.batches)
    _n0 = len(bld.objs)
    for base, post, light_transform in bases:
        for node, m in walk_tree(d, r, B, base, valid=objs, root_sib="roots" not in opts and "roottable" not in opts):
            if node["model"] and not (node["ev"] & 8):
                # Texture lists belong to NJS_OBJECTs, not to the hierarchy root.
                # The executable's object->texlist tables contain entries for
                # child nodes too.  Applying the root list to every child made
                # valid slots fall outside that list and baked them grey.
                noff = node["addr"] - B
                node_tmap = texmap_for(noff, ordinal) if TEXMAP is not None and noff in TEXMAP else root_tmap
                bld.add_model(d, node["model"], B, m, node_tmap, light=LIGHT, twosided=(off in FTILES), post_transform=post, light_matrix=mmul(light_transform,m) if light_transform is not None else None)
    if "roots" in opts or "roottable" in opts:   # one object per listed root (index = table index), merging the parts; empty roots keep a placeholder
        parts = [struct.unpack("<ffffIIII", o) for o in bld.objs[nobj0:]]; del bld.objs[nobj0:]
        if parts:
            cx = sum(p[0] for p in parts) / len(parts); cy = sum(p[1] for p in parts) / len(parts); cz = sum(p[2] for p in parts) / len(parts)
            rr = max(math.sqrt((p[0]-cx)**2 + (p[1]-cy)**2 + (p[2]-cz)**2) + p[3] for p in parts)
            bld.objs.append(struct.pack("<ffffIIII", cx, cy, cz, rr, nb0, len(bld.batches) - nb0, 0, 0))
        else: bld.objs.append(struct.pack("<ffffIIII", 0, 0, 0, 0, nb0, 0, 0, 0))
    SIDE += [[off, 'proto' if (LAYOUT is not None and off in PROTOSET) else 'root', len(bases)]] * (len(bld.objs) - _n0)
    ordinal += 1
for moff, pos, ang in MODEL_INST:
    _n0 = len(bld.objs); bld.add_model(d, B + moff, B, placement_matrix(pos, ang), (lambda s: FALLBACK), light=LIGHT); SIDE += [[moff, 'model', 1]] * (len(bld.objs) - _n0)
print(f"objects skipped (far-LOD/unplaced): {skipped}; model instances: {len(MODEL_INST)}")
_fb = [b for b in bld.batches if b[4] == FALLBACK]
print(f"fallback material: {len(_fb)} batches / {sum(b[3] for b in _fb) // 3} triangles")
if FALLBACK_USE:
    print("fallback source slots:", ", ".join("%x:%d=%d" % (o, s, n) for (o, s), n in sorted(FALLBACK_USE.items(), key=lambda x: -x[1])[:24]))
for _b in _fb: _b[5] |= 16   # JGS_B_FALLBACK: preserve provenance after texture-table compaction
print("wrote", out_path, write_jgs(out_path, bld, tex_imgs, texmax=TEXMAX), f"size {os.path.getsize(out_path):,}")
json.dump(SIDE, open(out_path + ".map.json", "w"))
