#!/usr/bin/env python3
"""Bake JSR characters (PLAY+PTEX) into a JGC1 file with bone-local meshes, skeletons and motion keys,
for runtime animation on the PSP.  usage:
  jgc_convert.py out.jgc [--motions=12] [--lod=0] [--preview=out.jgs] NAME:dat.bin:txr.bin:x,y,z,yaw ...
JGC1 layout (LE, 16-byte aligned sections):
  hdr  'JGC1' u32 n_chars n_bones n_batches n_verts n_idx n_tex n_motions ; u32 off_chars off_bones off_batches off_verts off_idx off_tex off_motions off_keys off_pix
  char {u32 bone0,nbones,motion0,nmotions; f32 x,y,z,yaw; char name[16]}                       48 B
  bone {s16 parent; u16 ev; f32 pos[3]; s32 ang[3]; f32 scl[3]; u32 batch0,nbatch}              48 B
  batch/vert/idx/tex as JGS1
  motion {u32 nframes; u32 chan0}  chan (per bone) {u32 npos, off_pos, nang, off_ang, nscl, off_scl}  pos/scl key f32[3] (12 B), ang key s16[3] (6 B)
  NAME:dat:txr:x,y,z,yaw:rawat:BASE:ROOTADDR:MOTADDR = raw image with explicit NJS_OBJECT root and motion header {nframes<<16|type, mdata*} (title logo bank)"""
import struct, sys, os, math, collections
sys.path.insert(0, os.path.dirname(__file__))
from legacy_ninjalib import *
import legacy_ninjalib as ninjalib
if "toon" in opts if 'opts' in dir() else False: pass
sys.path.insert(0, os.path.join(os.getcwd(), 'tools'))
from pvr_decode import decode as pvr_decode
args = [a for a in sys.argv[1:] if not a.startswith("--")]; opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--"))
out = args[0]; NMOT = int(opts.get("motions", 12)); LOD = int(opts.get("lod", 0)); ninjalib.TOON = "toon" in opts
chars = []; bones = []; bld = JgsBuilder(); tex_imgs = []; motions = []; keys = bytearray()
def align(b, n=4):
    while len(b) % n: b.append(0)
for spec in args[1:]:
    parts = spec.split(":"); name, datp, txrp, place = parts[:4]; x, y, z, yaw = [float(v) for v in place.split(",")]
    NM = NMOT
    for pp in parts[4:]:
        if pp.startswith("mot="): NM = int(pp[4:])
    parts = [pp for pp in parts if not pp.startswith("mot=")]
    d = open(datp, "rb").read(); t = open(txrp, "rb").read()
    u32 = lambda o: struct.unpack_from("<I", d, o)[0]
    RB_ = 0
    if len(parts) > 4 and parts[4] == "rawat":
        RB_ = int(parts[5], 16); roots = [int(parts[6], 16)]; s3 = None; raw_mots = [int(parts[7], 16) - RB_] if int(parts[7], 16) else []
    elif len(parts) > 4 and parts[4] == "raw":
        # raw memory image (ENEMY/*): base, skeleton root index, motion header offset auto-detected
        RB_ = int(parts[5], 16); ri = int(parts[6]) if len(parts) > 6 else 0
        Nd = len(d); W = struct.unpack_from("<%dI" % (Nd // 4), d)
        isp_ = lambda v: RB_ <= v < RB_ + Nd and (v & 3) == 0
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
            if not (0x10000 <= w < 0x400000 and 0 < (w & 0xffff) < 0x200 and isp_(W[i + 1])): continue
            md = W[i + 1] - RB_; ok = True
            for bi in range(nnodes):
                r = md + bi * 16
                if r + 16 > Nd: ok = False; break
                nb, p0, p1, p2 = struct.unpack_from("<4I", d, r)
                if nb >= 0x200 or not all(v == 0 or isp_(v) for v in (p0, p1, p2)): ok = False; break
            if ok: mots_.append(i * 4)
        print(f"  raw {name}: base {RB_:#x} roots {len(rts)} (sizes {[size(a) for a in rts[:6]]}) using root {ri} ({nnodes} nodes); motion headers {[hex(m) for m in mots_][:8]}")
        roots = [root]; s3 = None; raw_mots = mots_
    else:
        secs = []; o = u32(4)
        while u32(o): secs.append((u32(o), u32(o + 4))); o += 8
        roots = [x_ for ty, x_ in secs if ty == 1]; s3 = [x_ for ty, x_ in secs if ty == 3][0]; raw_mots = None
    # textures
    tex0 = len(tex_imgs); i = 0
    while True:
        i = t.find(b"GBIX", i)
        if i < 0: break
        j = t.find(b"PVRT", i + 8, i + 64); L = struct.unpack_from("<I", t, j + 4)[0]
        try: tex_imgs.append(pvr_decode(t[j:j + 8 + L]))
        except Exception: tex_imgs.append(Image.new("RGBA", (8, 8), (255, 0, 255, 255)))
        i += 8
    ntex = len(tex_imgs) - tex0
    # PTEX node table: 'PTEX', off_list, off_table; list = pairs {texlist* -> {names*, n}, ptr -> PVR offsets[n]}; table = u32 count then
    # per node ordinal (tree order) a texlist pointer (-1 terminated). A strip's TINY id s selects texlist[s] of the node's list.
    node_tex = []
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
        ot = tu(8); k = 0
        while k < 64 and tu(ot + 4 + 4 * k) != 0xffffffff: node_tex.append(tl.get(tu(ot + 4 + 4 * k), [])); k += 1
    # skeleton (tree order = motion channel order), bone-local meshes
    bone0 = len(bones); order = []; parent_of = {}; bms = {-1: [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]}; vcache = {}
    stack = [(roots[LOD if not raw_mots is not None else 0] if raw_mots is None else roots[0], -1)]
    while stack:
        a, par = stack.pop()
        if not a: continue
        n = read_object(d, a, RB_); idx = len(bones); order.append(a); parent_of[a] = par
        bm = mmul(bms[par], node_matrix(n["ev"], n["pos"], n["ang"], n["scl"])); bms[idx] = bm   # bind-pose world matrix of this node
        b0 = len(bld.batches)
        ordn = len(order) - 1; ntl = node_tex[ordn] if ordn < len(node_tex) and node_tex[ordn] else None
        tmapn = (lambda s, t0=tex0, nt=ntex, L=ntl: t0 + (L[s] if s < len(L) else L[0])) if ntl else (lambda s, t0=tex0, nt=ntex: t0 + min(s, nt - 1))
        if n["model"]: bld.add_model(d, n["model"], RB_, [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1], tmapn, light=("nolight" not in opts), cache=vcache, bindm=bm, bindm_inv=minv(bm))
        bones.append([par, n["ev"], n["pos"], n["ang"], n["scl"], b0, len(bld.batches) - b0])
        if n["sib"]: stack.append((n["sib"], par))
        if n["child"]: stack.append((n["child"], idx))
    nb = len(bones) - bone0
    # motions
    mot0 = len(motions)
    for mi in range(NM):
        if raw_mots is not None:
            if mi >= len(raw_mots): break
            mot = raw_mots[mi]
        else:
            mot = u32(s3 + mi * 4)
            if not mot: break
        md = u32(mot + 4) - RB_; nframes = max(u32(mot) >> 16, 1); chans = []
        for bi in range(nb):
            r = md + bi * 16; nk, pk, ak, sk = struct.unpack_from("<4I", d, r); nkeys = nframes if (pk or ak or sk) else 0
            op = oa = osc = 0; npos = nang = nscl = 0
            pk = pk - RB_ if pk else 0; ak = ak - RB_ if ak else 0; sk = sk - RB_ if sk else 0
            if pk: align(keys, 4); op = len(keys); keys += d[pk:pk + nkeys * 12]; npos = nkeys
            if ak: align(keys, 2); oa = len(keys); keys += d[ak:ak + nkeys * 6]; nang = nkeys
            if sk: align(keys, 4); osc = len(keys); keys += d[sk:sk + nkeys * 12]; nscl = nkeys
            chans.append((npos, op, nang, oa, nscl, osc))
        motions.append((nframes, chans))
    chars.append((bone0, nb, mot0, len(motions) - mot0, x, y, z, yaw, name))
    print(f"{name}: bones {nb} batches {len(bld.batches)} motions {len(motions)-mot0} textures {ntex}")
# ---- write ----
tex_recs = []; pix = bytearray(); tindex = {}
def alignb(b, n=16):
    while len(b) % n: b.append(0)
for ti in sorted(bld.used):
    im = tex_imgs[ti]; w, h = im.size
    if "texwhite" in opts: im = Image.merge("RGBA", (Image.new("L", im.size, 255),) * 3 + (im.convert("RGBA").split()[3],))   # alpha-mask textures (title copyright: black texels drawn white by the DC)
    while max(w, h) > int(opts.get("texmax",128)): w //= 2; h //= 2
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
    f.write(b"JGC1" + struct.pack("<7I", len(chars), len(bones), len(batches), bld.nv, bld.ni, len(tex_recs), len(motions)))
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
                    u, v, c, px, py, pz = struct.unpack_from("<ffIfff", bld.verts, (vo + vi) * 24)
                    if vi not in vd: vd[vi] = (len(vd), (u, v, c, xform(m, (px, py, pz))))
                    il.append(vd[vi][0])
                vlist = sorted(vd.values(), key=lambda t: t[0]); voff = pb.nv
                for _, (u, v, c, wp) in vlist: pb.verts += struct.pack("<ffIfff", u, v, c, *wp)
                ioff = pb.ni; pb.idx += struct.pack("<%dH" % len(il), *il); pb.nv += len(vlist); pb.ni += len(il); pb.used.add(tex)
                pb.batches.append([voff, ioff, len(vlist), len(il), tex, fl])
                if pb.ni & 1: pb.idx += b"\0\0"; pb.ni += 1
                pts = [t[1][3] for t in vlist]; cx = sum(p[0] for p in pts)/len(pts); cy = sum(p[1] for p in pts)/len(pts); cz = sum(p[2] for p in pts)/len(pts)
                r = math.sqrt(max((p[0]-cx)**2+(p[1]-cy)**2+(p[2]-cz)**2 for p in pts))
                for p in pts:
                    for i in range(3): pb.bmin[i] = min(pb.bmin[i], p[i]); pb.bmax[i] = max(pb.bmax[i], p[i])
                pb.objs.append(struct.pack("<ffffIIII", cx, cy, cz, r, len(pb.batches) - 1, 1, 0, 0))
    print("preview:", write_jgs(opts["preview"], pb, tex_imgs))
