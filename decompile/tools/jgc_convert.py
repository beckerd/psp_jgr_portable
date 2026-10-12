#!/usr/bin/env python3
"""Bake JSR characters (PLAY+PTEX) into a JGC2 file with owner-local meshes, skeletons and motion keys,
for runtime animation on the PSP.  usage:
  jgc_convert.py out.jgc [--motions=12] [--lod=0] [--preview=out.jgs] [--spec=rows.txt] NAME:dat.bin:txr.bin:x,y,z,yaw ...
JGC2 layout (LE, 16-byte aligned sections):
  hdr  'JGC2' u32 n_chars n_bones n_batches n_verts n_idx n_tex n_motions ; u32 off_chars off_bones off_batches off_verts off_idx off_tex off_motions off_keys off_pix
  char {u32 bone0,nbones,motion0,nmotions; f32 x,y,z,yaw; char name[16]}                       48 B
  bone {s16 parent; u16 ev; f32 pos[3]; s32 ang[3]; f32 scl[3]; u32 batch0,nbatch}              48 B
  batch/idx/tex as JGS1; vert {f32 u,v; u32 ABGR; f32 x,y,z; u16 ownerBone,pad}
  motion {u32 nframes; u32 chan0}  chan (per bone) {u32 npos, off_pos, nang, off_ang, nscl, off_scl}  pos/scl key f32[3] (12 B), ang key s16[3] (6 B)
  NAME:dat:txr:x,y,z,yaw:rawat:BASE:ROOTADDR:MOTADDR[,MOTADDR...]:tex=N = raw image with explicit NJS_OBJECT root,
  one or more motion headers {nframes<<16|type, mdata*}, and an optional explicit PVR index (title/people banks).
  Raw rows may instead use texmap=N,N,... to reproduce an NJS_TEXLIST whose slots do not follow PVR file order.
  Public raw rows use texbinding=KIND,BASE,ARRAY,COUNT,TEXBASE,MODE to read that mapping from the user's executable.
  material=1 retains original strip blend factors for one model in a shared bank.
  cull=none overrides legacy strip-derived face culling when verified in the source renderer.
  --texmax-overrides=SOURCE_INDEX:SIZE,... retains detail for selected close-up textures."""
import struct, sys, os, math, collections
sys.path.insert(0, os.path.dirname(__file__))
from ninjalib import *
import ninjalib
if "toon" in opts if 'opts' in dir() else False: pass
from pvr_decode import decode as pvr_decode
from texture_binding import resolve_texture_binding
args = [a for a in sys.argv[1:] if not a.startswith("--")]; opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--"))
if "spec" in opts:
    args.extend(line.strip() for line in open(opts["spec"]) if line.strip() and not line.lstrip().startswith("#"))
out = args[0]; NMOT = int(opts.get("motions", 12)); LOD = int(opts.get("lod", 0)); TEXMAX = int(opts.get("texmax", 128)); ninjalib.TOON = "toon" in opts
TEXMAX_OVERRIDES = dict(tuple(map(int, item.split(':'))) for item in opts.get('texmax-overrides', '').split(',') if item)
if any(i < 0 or cap < 16 or cap > 512 or cap & (cap-1) for i, cap in TEXMAX_OVERRIDES.items()):
    raise ValueError('texture override needs a source index and power-of-two cap from 16 to 512')
if 'baked-light' in opts:
    from baked_light import PROFILES
    ninjalib.BAKED_LIGHT = opts['baked-light']; assert ninjalib.BAKED_LIGHT in PROFILES
chars = []; bones = []; bld = JgsBuilder(); tex_imgs = []; motions = []; keys = bytearray(); key_cache = {}; tex_bank_cache = {}
def align(b, n=4):
    while len(b) % n: b.append(0)
def store_keys(blob, alignment):
    """Store a key stream once. PLAY banks share many motions verbatim
    between skaters; preserving those shared streams saves several MiB on a
    PSP-1000 without quantising or otherwise changing the Dreamcast data."""
    cache_key = (alignment, blob)
    if cache_key in key_cache: return key_cache[cache_key]
    align(keys, alignment); off = len(keys); keys.extend(blob); key_cache[cache_key] = off
    return off
for spec in args[1:]:
    parts = spec.split(":"); name, datp, txrp, place = parts[:4]; x, y, z, yaw = [float(v) for v in place.split(",")]
    NM = NMOT; raw_tex = None; raw_texmap = None; raw_binding = None; effect_blend = None; cull = None; motion_image = None; source_lighting = 0; force_alpha = False
    source_material = 'source-material' in opts
    play_variant = {'spray':0x24,'companion':0x54}.get(opts.get('play-variant'),4)
    for pp in parts[4:]:
        if pp.startswith("mot="): NM = int(pp[4:])
        elif pp.startswith("tex="): raw_tex = int(pp[4:])
        elif pp.startswith("texmap="): raw_texmap = [int(value) for value in pp[7:].split(",")]
        elif pp.startswith("texbinding="):
            if raw_binding is not None: raise ValueError('Duplicate source texture binding')
            raw_binding = pp[11:]
        elif pp.startswith("blend="): effect_blend = [int(value) for value in pp[6:].split(",")]
        elif pp.startswith("cull="): cull = pp[5:]
        elif pp.startswith("motionimage="): motion_image = pp[12:].rsplit("@", 1)
        elif pp.startswith("lighting="): source_lighting = int(pp[9:]);assert source_lighting in (1,2)
        elif pp == "alpha=1": force_alpha = True
        elif pp == "material=1": source_material = True
    if effect_blend is not None:
        assert len(effect_blend)==2 and all(0<=value<8 for value in effect_blend)
    assert cull in (None, "none"), f"unsupported explicit cull mode: {cull}"
    if raw_binding is not None and (raw_tex is not None or raw_texmap is not None):
        raise ValueError('Source texture binding conflicts with explicit texture selection')
    parts = [pp for pp in parts if not pp.startswith(("mot=", "tex=", "texmap=", "texbinding=", "blend=", "cull=", "motionimage=", "lighting=", "alpha=", "material="))]
    d = open(datp, "rb").read(); t = open(txrp, "rb").read()
    if 'texture-sector-tail' in opts:
        from pathlib import Path
        from cutscene_sources import texture_image
        t = texture_image(Path(txrp).resolve())
    if raw_binding is not None:
        raw_texmap = resolve_texture_binding(raw_binding, t)
    u32 = lambda o: struct.unpack_from("<I", d, o)[0]
    RB_ = 0
    if len(parts) > 4 and parts[4] == "rawat":
        RB_ = int(parts[5], 16); roots = [int(parts[6], 16)]; s3 = None
        raw_mots = [int(a, 16) - RB_ for a in parts[7].split(",") if int(a, 16)]
    elif len(parts) > 4 and parts[4] == "raw":
        # raw memory image (ENEMY/*): base, skeleton root index, motion header offset auto-detected
        RB_ = int(parts[5], 16); ri = int(parts[6]) if len(parts) > 6 else 0
        Nd = len(d); W = struct.unpack_from("<%dI" % (Nd // 4), d)
        isp_ = lambda v, alignment=4: RB_ <= v < RB_ + Nd and v % alignment == 0
        # objects: NJS_OBJECT heuristics
        objs_ = {}
        for oo in range(0, Nd - 0x34, 4):
            ev = u32(oo)
            if ev > 0xffff: continue
            mo, ch, sb = u32(oo + 4), u32(oo + 0x2c), u32(oo + 0x30)
            if not all(v == 0 or isp_(v) for v in (mo, ch, sb)) or (mo == 0 and ch == 0 and sb == 0): continue
            scl = struct.unpack_from("<3f", d, oo + 0x20)
            if not all(1e-4 < v < 1e3 for v in scl): continue
            objs_[RB_ + oo] = (ch, sb)
        kids = set(v for c in objs_.values() for v in c)
        rts = sorted(a for a in objs_ if a not in kids)
        # rank roots by subtree size, pick ri-th largest
        def size(a):
            n = 0; st = [a]
            while st:
                q = st.pop()
                if not q or q not in objs_: continue
                n += 1; st += list(objs_[q])
            return n
        rts.sort(key=lambda a: -size(a)); root = rts[ri]; nnodes = size(root)
        # motion headers: {type<<16|nb, mdata*} where mdata block has nnodes records {nb<0x200, ptr|0, ptr|0, ptr|0}
        mots_ = []
        for i in range(len(W) - 1):
            w = W[i]
            if not (0x10000 <= w < 0x1000000 and 0 < (w & 0xffff) < 0x200 and isp_(W[i + 1])): continue
            md = W[i + 1] - RB_; ok = True
            for bi in range(nnodes):
                r = md + bi * 16
                if r + 16 > Nd: ok = False; break
                nb, p0, p1, p2 = struct.unpack_from("<4I", d, r)
                # Ninja angle-key streams are s16[3] and are only guaranteed
                # two-byte alignment; rejecting them as non-u32-aligned was
                # silently dropping most ENEMY animations.
                if nb >= 0x200 or not (p0 == 0 or isp_(p0)) or not (p1 == 0 or isp_(p1, 2)) or not (p2 == 0 or isp_(p2)): ok = False; break
            if ok: mots_.append(i * 4)
        print(f"  raw {name}: base {RB_:#x} roots {len(rts)} (sizes {[size(a) for a in rts[:6]]}) using root {ri} ({nnodes} nodes); motion headers {[hex(m) for m in mots_][:8]}")
        roots = [root]; s3 = None; raw_mots = mots_
    else:
        secs = []; o = u32(4)
        while u32(o): secs.append((u32(o), u32(o + 4))); o += 8
        roots = [x_ for ty, x_ in secs if ty == 1]; s3 = [x_ for ty, x_ in secs if ty == 3][0]; raw_mots = None
        if play_variant != 4:
            assert d[:4] == b'PLAY' and LOD == 0
            roots = [u32(u32(8) + play_variant)]
            if play_variant == 0x54:
                table=u32(u32(8)+0x68)
                raw_mots=[u32(table+4*i) for i in range(3)]
                NM=3
    # textures
    tex_key = os.path.abspath(txrp)
    if tex_key in tex_bank_cache:
        tex0, ntex = tex_bank_cache[tex_key]
    else:
        tex0 = len(tex_imgs); i = 0
        # Full cutscene imports supply the loader's exact GBIX pointers.
        # A few original TXRs contain unused trailing pixels; do not scan
        # those for magic or silently replace malformed textures in this mode.
        explicit_offsets = [int(v, 0) for v in opts['texture-offsets'].split(',') if v] if 'texture-offsets' in opts else None
        scan_offsets = []
        if explicit_offsets is None:
            while True:
                i = t.find(b"GBIX", i)
                if i < 0: break
                scan_offsets.append(i); i += 8
        for i in explicit_offsets if explicit_offsets is not None else scan_offsets:
            if explicit_offsets is not None:
                assert 0 <= i <= len(t)-8 and t[i:i+4] == b'GBIX'
                j = i + 8 + struct.unpack_from('<I', t, i+4)[0]
                assert j+16 <= len(t) and t[j:j+4] == b'PVRT'
            else: j = t.find(b"PVRT", i + 8, i + 64)
            L = struct.unpack_from("<I", t, j + 4)[0]
            if explicit_offsets is not None: assert j+8+L <= len(t)
            try: tex_imgs.append(pvr_decode(t[j:j + 8 + L]))
            except Exception:
                if explicit_offsets is not None: raise
                tex_imgs.append(Image.new("RGBA", (8, 8), (255, 0, 255, 255)))
        ntex = len(tex_imgs) - tex0; tex_bank_cache[tex_key] = (tex0, ntex)
    # PTEX: 'PTEX', off_list, off_table.  The table begins with the game's
    # character id, then action/variant NJS_TEXLIST pointers.  It is not a
    # per-skeleton-node table: the original player draw calls njSetTexture
    # once with the first list (player work +0x558; Beat resolves to PTEX
    # +0xe0) before traversing the whole 20-node model.  Treating successive
    # table entries as bone lists assigned Beat's glow/face textures to his
    # second arm and produced the conspicuous white shoulder bulge.
    model_tex = None
    if t[:4] == b"PTEX":
        tu = lambda o: struct.unpack_from("<I", t, o)[0]
        pv = {}; i = 0
        while True:
            i = t.find(b"GBIX", i)
            if i < 0: break
            pv[i] = len(pv); i += 8
        tl = {}; o = tu(4)
        while tu(o):
            tlp, arr = tu(o), tu(o + 4); tl[tlp] = [pv.get(tu(arr + 4 * k), 0) for k in range(tu(tlp + 4))]; o += 8
        ot = tu(8)
        if ot + play_variant + 4 <= len(t) and tu(ot + play_variant) != 0xffffffff:
            model_tex = tl.get(tu(ot + play_variant), [])
            if play_variant != 4 and tu(ot + play_variant): assert model_tex, 'missing original PLAY variant texture list'
    # skeleton (tree order = motion channel order), bone-local meshes
    bone0 = len(bones); order = []; parent_of = {}; bms = {-1: [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]}; vcache = {}
    stack = [(roots[LOD if not raw_mots is not None else 0] if raw_mots is None else roots[0], -1)]
    while stack:
        a, par = stack.pop()
        if not a: continue
        n = read_object(d, a, RB_); idx = len(bones); order.append(a); parent_of[a] = par
        if play_variant == 0x54 and n['model']: assert model_tex, 'mouth geometry without original texture list'
        bm = mmul(bms[par], node_matrix(n["ev"], n["pos"], n["ang"], n["scl"])); bms[idx] = bm   # bind-pose world matrix of this node
        b0 = len(bld.batches)
        ntl = model_tex
        if raw_tex is not None:
            if raw_tex < 0 or raw_tex >= ntex: raise ValueError(f"{name}: explicit texture {raw_tex} outside {ntex}-texture bank")
            tmapn = lambda s, t0=tex0, ti=raw_tex: t0 + ti
        elif raw_texmap is not None:
            if not raw_texmap or any(value < 0 or value >= ntex for value in raw_texmap):
                raise ValueError(f"{name}: explicit texture map {raw_texmap} outside {ntex}-texture bank")
            def tmapn(s, t0=tex0, tm=tuple(raw_texmap), char_name=name):
                if s < 0 or s >= len(tm):
                    raise ValueError(f"{char_name}: model texture slot {s} outside explicit {len(tm)}-slot map")
                return t0 + tm[s]
        else:
            tmapn = (lambda s, t0=tex0, nt=ntex, L=ntl: t0 + (L[s] if s < len(L) else L[0])) if ntl else (lambda s, t0=tex0, nt=ntex: t0 + min(s, nt - 1))
        if n["model"]: bld.add_model(d, n["model"], RB_, [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1], tmapn, light=("nolight" not in opts and not source_lighting), cache=vcache, bindm=bm, bindm_inv=minv(bm), owner=idx-bone0, source_material=source_material,source_environment=("source-environment" in opts),source_lighting=source_lighting,retain_lighting=(2 if d[:4]==b'PLAY' else 0) if 'lighting-sidecar' in opts else None)
        if effect_blend is not None:
            for batch in bld.batches[b0:]:
                batch[5] |= 16 | (effect_blend[0]<<5) | (effect_blend[1]<<8)
        if force_alpha:
            for batch in bld.batches[b0:]: batch[5] |= 1
        if cull == "none":
            for batch in bld.batches[b0:]: batch[5] |= 2
        bones.append([par, n["ev"], n["pos"], n["ang"], n["scl"], b0, len(bld.batches) - b0])
        if n["sib"]: stack.append((n["sib"], par))
        if n["child"]: stack.append((n["child"], idx))
    nb = len(bones) - bone0
    # motions
    mot0 = len(motions)
    motion_data, motion_base = (open(motion_image[0], "rb").read(), int(motion_image[1], 16)) if motion_image else (d, RB_)
    def mu(offset): return struct.unpack_from("<I", motion_data, offset)[0]
    def channel_bytes(address, length):
        offset = address - motion_base
        if offset < 0 or offset + length > len(motion_data):
            raise ValueError(f"{name}: motion channel {address:#x}+{length} outside source image")
        return motion_data[offset:offset + length]
    for mi in range(NM):
        if raw_mots is not None:
            if mi >= len(raw_mots): break
            mot = raw_mots[mi]
        else:
            mot = u32(s3 + mi * 4)
            if not mot: break
        mot += RB_ - motion_base
        md = mu(mot + 4) - motion_base; nframes = max(mu(mot) >> 16, 1); chans = []
        for bi in range(nb):
            r = md + bi * 16; nk, pk, ak, sk = struct.unpack_from("<4I", motion_data, r); nkeys = nframes if (pk or ak or sk) else 0
            op = oa = osc = 0; npos = nang = nscl = 0
            if pk: op = store_keys(channel_bytes(pk, nkeys * 12), 4); npos = nkeys
            if ak: oa = store_keys(channel_bytes(ak, nkeys * 6), 2); nang = nkeys
            if sk: osc = store_keys(channel_bytes(sk, nkeys * 12), 4); nscl = nkeys
            chans.append((npos, op, nang, oa, nscl, osc))
        motions.append((nframes, chans))
    chars.append((bone0, nb, mot0, len(motions) - mot0, x, y, z, yaw, name))
    print(f"{name}: bones {nb} batches {len(bld.batches)} motions {len(motions)-mot0} textures {ntex}")
# ---- write ----
tex_recs = []; pix = bytearray(); tindex = {}
if opts.get('keep-textures') == 'all':
    # Source texlist rotation (0ecaac) can select textures unused by the
    # bind-pose strips. Preserve the source PVR indices for that runtime.
    bld.used.update(range(len(tex_imgs)))
elif 'keep-textures' in opts:
    # Animated texlists may use just a small subset of a large stage bank.
    keep=[int(i) for i in opts['keep-textures'].split(',')]
    if any(i<0 or i>=len(tex_imgs) for i in keep):raise ValueError('retained texture outside source bank')
    bld.used.update(keep)
def alignb(b, n=16):
    while len(b) % n: b.append(0)
if set(TEXMAX_OVERRIDES) - bld.used:
    raise ValueError('texture override refers to an unused or missing source image')
for ti in sorted(bld.used):
    im = tex_imgs[ti]; w, h = im.size
    if "texwhite" in opts: im = Image.merge("RGBA", (Image.new("L", im.size, 255),) * 3 + (im.convert("RGBA").split()[3],))   # alpha-mask textures (title copyright: black texels drawn white by the DC)
    cap = TEXMAX_OVERRIDES.get(ti, TEXMAX)
    while max(w, h) > cap: w //= 2; h //= 2
    w = max(w, 16); h = max(h, 8); im = im.resize((w, h), Image.LANCZOS); has_alpha = im.getextrema()[3][0] < 255
    q = im.quantize(256, method=Image.Quantize.FASTOCTREE); pal = q.getpalette("RGBA") or q.getpalette(); clut = bytearray()
    for i in range(256):
        if pal and (i*4+3) < len(pal): r, g, b, a = pal[i*4:i*4+4]
        elif pal and (i*3+2) < len(pal): r, g, b = pal[i*3:i*3+3]; a = 255
        else: r = g = b = a = 0
        clut += bytes((r, g, b, a))
    alignb(pix); oc = len(pix); pix += clut; alignb(pix); op = len(pix); pix += swizzle8(q.tobytes(), w, h)
    tindex[ti] = len(tex_recs); tex_recs.append([w, h, 1 if has_alpha else 0, oc, op])
batches = []
for vo, io, nv, ni, tex, fl in bld.batches:
    ti = tindex.get(tex, 0)
    if tex != 0xffff and tex_recs[ti][2]: fl |= 1
    batches.append(struct.pack("<IIHHHH", vo, io, nv, ni, ti, fl))
chan_blob = bytearray(); mot_recs = []
for nframes, chans in motions:
    mot_recs.append(struct.pack("<II", nframes, len(chan_blob)))
    for npos, op, nang, oa, nscl, osc in chans: chan_blob += struct.pack("<6I", npos, op, nang, oa, nscl, osc)
sections = [("chars", b"".join(struct.pack("<4I4f16s", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8].encode()[:15]) for c in chars)),
            ("bones", b"".join(struct.pack("<hH3f3i3fII", b[0], b[1], *b[2], *b[3], *b[4], b[5], b[6]) for b in bones)),
            ("batches", b"".join(batches)), ("verts", bytes(bld.verts)), ("idx", bytes(bld.idx)),
            ("tex", None), ("motions", b"".join(mot_recs) + bytes(chan_blob)), ("keys", bytes(keys)), ("pix", bytes(pix))]
hdr_size = 4 + 7*4 + 9*4; off = (hdr_size + 15) & ~15; offs = {}
for nm, blob in sections:
    offs[nm] = off; L = 16 * len(tex_recs) if nm == "tex" else len(blob); off = (off + L + 15) & ~15
with open(out, "wb") as f:
    f.write((b"JGC3" if bld.vertex_stride==40 else b"JGC2") + struct.pack("<7I", len(chars), len(bones), len(batches), bld.nv, bld.ni, len(tex_recs), len(motions)))
    f.write(struct.pack("<9I", *[offs[k] for k in ("chars", "bones", "batches", "verts", "idx", "tex", "motions", "keys", "pix")]))
    def pad(n):
        while f.tell() < n: f.write(b"\0")
    for nm, blob in sections:
        pad(offs[nm])
        if nm == "tex":
            for w, h, fl, oc, op in tex_recs: f.write(struct.pack("<HHHHII", w, h, fl, 0, offs["pix"] + oc, offs["pix"] + op))
        elif nm == "motions":
            # motion records need absolute chan offsets: rewrite
            base = offs["motions"] + 8 * len(motions)
            for (nframes, chans), rec in zip(motions, mot_recs):
                _, rel = struct.unpack("<II", rec); f.write(struct.pack("<II", nframes, base + rel))
            f.write(bytes(chan_blob))
        else: f.write(blob)
print(f"wrote {out}: {os.path.getsize(out):,} bytes; chars {len(chars)} bones {len(bones)} batches {len(batches)} tris {bld.ni//3} tex {len(tex_recs)} motions {len(motions)} keys {len(keys):,} B")
if 'lighting-sidecar' in opts:
    # A separate, hash-bound original material/normal stream preserves every
    # byte of the shared bank, including legacy city lighting and motion keys.
    assert bld.vertex_stride==28 and len(bld.light_records)==bld.nv*20
    digest=2166136261
    # Exclude legacy baked colour. It does not identify a vertex/owner and
    # depends on the old export's lighting option, which we do not replace.
    for offset,value in enumerate(bld.verts):
        if not 8<=offset%28<12:digest=((digest^value)*16777619)&0xffffffff
    with open(opts['lighting-sidecar'],'wb') as f:
        f.write(b'PLT1'+struct.pack('<4I',bld.nv,len(chars),digest,20));f.write(bld.light_records)
# ---- optional preview: evaluate frame 0 of each char's first motion into a JGS ----
if "preview" in opts:
    pb = JgsBuilder()
    kb = bytes(keys)
    for ci, (bone0, nb, mot0, nm, x, y, z, yaw, name) in enumerate(chars):
        base = mmul(mtrans(x, y, z), mroty(math.radians(yaw))); mats = []
        nframes, chans = motions[mot0]; fr = int(opts.get("frame", 0)); k = min(fr, nframes - 1)
        for bi in range(nb):
            par, ev, pos, ang, scl, b0, nbat = bones[bone0 + bi]; npos, op, nang, oa, nscl, osc = chans[bi]
            if npos: pos = struct.unpack_from("<3f", kb, op + min(k, npos - 1) * 12)
            if nang: ang = struct.unpack_from("<3h", kb, oa + min(k, nang - 1) * 6)
            evf = ev & ~3 if (npos or nang) else ev
            m = mmul(mats[par - bone0] if par >= 0 else base, node_matrix(evf, pos, ang, scl)); mats.append(m)
            for b in range(b0, b0 + nbat):
                vo, io, nv, ni, tex, fl = bld.batches[b]
                # transform baked local verts
                vd = {}; il = []
                for i in range(ni):
                    vi = struct.unpack_from("<H", bld.idx, (io + i) * 2)[0]
                    u, v, c, px, py, pz = struct.unpack_from("<ffIfff", bld.verts, (vo + vi) * bld.vertex_stride)
                    if vi not in vd: vd[vi] = (len(vd), (u, v, c, xform(m, (px, py, pz))))
                    il.append(vd[vi][0])
                vlist = sorted(vd.values(), key=lambda t: t[0]); voff = pb.nv
                for _, (u, v, c, wp) in vlist: pb.verts += struct.pack("<ffIfff", u, v, c, *wp)
                ioff = pb.ni; pb.idx += struct.pack("<%dH" % len(il), *il); pb.nv += len(vlist); pb.ni += len(il)
                if tex != 0xffff: pb.used.add(tex)
                pb.batches.append([voff, ioff, len(vlist), len(il), tex, fl])
                if pb.ni & 1: pb.idx += b"\0\0"; pb.ni += 1
                pts = [t[1][3] for t in vlist]; cx = sum(p[0] for p in pts)/len(pts); cy = sum(p[1] for p in pts)/len(pts); cz = sum(p[2] for p in pts)/len(pts)
                r = math.sqrt(max((p[0]-cx)**2+(p[1]-cy)**2+(p[2]-cz)**2 for p in pts))
                for p in pts:
                    for i in range(3): pb.bmin[i] = min(pb.bmin[i], p[i]); pb.bmax[i] = max(pb.bmax[i], p[i])
                pb.objs.append(struct.pack("<ffffIIII", cx, cy, cz, r, len(pb.batches) - 1, 1, 0, 0))
    print("preview:", write_jgs(opts["preview"], pb, tex_imgs))
