"""Shared Sega Ninja chunk-model parsing + JGS1 writer (buffer-based; pointers are absolute with base B)."""
import struct, math, collections
from PIL import Image

VSTRIDE = {0x20: 16, 0x21: 32, 0x22: 12, 0x23: 16, 0x24: 16, 0x25: 16, 0x26: 16, 0x27: 16, 0x28: 16,
           0x29: 24, 0x2a: 28, 0x2b: 28, 0x2c: 28, 0x2d: 28, 0x2e: 28, 0x2f: 28, 0x30: 16, 0x31: 16, 0x32: 16}
STRIP_EXTRA = {64: 0, 65: 2, 66: 2, 67: 3, 68: 5, 69: 5, 70: 2, 71: 4, 72: 4, 73: 1, 74: 3, 75: 3}

def parse_vlist(d, o):
    u16 = lambda x: struct.unpack_from("<H", d, x)[0]; u32 = lambda x: struct.unpack_from("<I", d, x)[0]
    verts = {}
    while True:
        t = d[o]
        if t == 0xff: break
        if t == 0: o += 4; continue
        size = u16(o + 2); ioff = u16(o + 4); nb = u16(o + 6); p = o + 8; st = VSTRIDE[t]
        for i in range(nb):
            pos = struct.unpack_from("<3f", d, p); nrm = col = None
            if t == 0x21: nrm = struct.unpack_from("<3f", d, p + 16)
            elif t == 0x23: col = u32(p + 12)
            elif t in (0x29, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f): nrm = struct.unpack_from("<3f", d, p + 12)
            elif t == 0x2a: nrm = struct.unpack_from("<3f", d, p + 12); col = u32(p + 24)
            verts[ioff + i] = (pos, nrm, col); p += st
        o += 4 + size * 4
    return verts

def parse_plist(d, o, source_material=False, source_environment=False):
    """returns list of (texid, stripflags, diffuseARGB, ((i,uv),(i,uv),(i,uv)))"""
    u16 = lambda x: struct.unpack_from("<H", d, x)[0]; s16 = lambda x: struct.unpack_from("<h", d, x)[0]; u32 = lambda x: struct.unpack_from("<I", d, x)[0]
    texid = 0; diffuse = 0xffffffff; tris = []; blend = 0x25
    while True:
        t = d[o]; fl = d[o + 1]
        if t == 0xff: break
        if t == 0: o += 2; continue
        if 1 <= t <= 5:
            if t == 1: blend = fl
            o += 2; continue
        if 8 <= t <= 9: texid = u16(o + 2) & 0x1fff; o += 4; continue
        if 16 <= t <= 31:
            blend = fl
            if t & 1: diffuse = u32(o + 4)
            o += 4 + u16(o + 2) * 2; continue
        if 56 <= t <= 58: o += 4 + u16(o + 2) * 2; continue
        if 64 <= t <= 75:
            size = u16(o + 2); n = u16(o + 4); cnt = n & 0x3fff; uf = n >> 14; p = o + 6
            ex = STRIP_EXTRA[t]; uvs = 255.0 if t in (65, 68, 71, 73) else 1023.0 if t in (66, 69, 72, 74) else 1.0
            has_uv = t in (65, 66, 68, 69, 71, 72)
            for s in range(cnt):
                ln = s16(p); p += 2; flip = ln < 0; ln = abs(ln); vs = []
                for k in range(ln):
                    idx = u16(p); uv = (s16(p + 2) / uvs, s16(p + 4) / uvs) if has_uv else (0.0, 0.0)
                    vs.append((idx, uv)); p += 2 + ex * 2
                p += max(ln - 2, 0) * uf * 2
                for k in range(ln - 2):
                    a, b, c = vs[k], vs[k + 1], vs[k + 2]
                    if (k & 1) ^ flip: a, b = b, a
                    flags = fl | ((16 | ((blend >> 3 & 7) << 5) | ((blend & 7) << 8)) << 8) if source_material else fl
                    environment=source_environment and t==64 and bool(fl&0x40)
                    if environment:flags|=2048<<8
                    tris.append((texid if has_uv or environment else 0xffff, flags, diffuse, (a, b, c)))
            o += 4 + size * 2; continue
        raise ValueError(f"plist chunk {t:#x} at +{o:x}")
    return tris

# ---- matrices (row-major 4x4) ----
def mmul(a, b): return [sum(a[i * 4 + k] * b[k * 4 + j] for k in range(4)) for i in range(4) for j in range(4)]
def minv(m):
    """general 4x4 inverse (Gauss-Jordan) for bind-pose matrices"""
    a = [list(m[i*4:i*4+4]) + [1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    for c in range(4):
        p = max(range(c, 4), key=lambda r: abs(a[r][c])); a[c], a[p] = a[p], a[c]; pv = a[c][c] or 1e-12
        a[c] = [v / pv for v in a[c]]
        for r in range(4):
            if r != c and a[r][c]: f = a[r][c]; a[r] = [v - f * w for v, w in zip(a[r], a[c])]
    return [a[i][4 + j] for i in range(4) for j in range(4)]
def mtrans(x, y, z): return [1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1]
def mscale(x, y, z): return [x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1]
def mrotx(a): c, s = math.cos(a), math.sin(a); return [1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0, 0, 0, 0, 1]
def mroty(a): c, s = math.cos(a), math.sin(a); return [c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1]
def mrotz(a): c, s = math.cos(a), math.sin(a); return [c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
A2R = 2 * math.pi / 65536.0
def placement_matrix(pos, ang):
    """031018/03144c: translate, then 27a198's Z, Y, X rotations.

    Placement angles are independent of the prototype's NJS eval flags.
    Reversing this order pitches hillside railings around the world axes
    instead of their authored heading, burying one end in the pavement.
    """
    rx, ry, rz = [a * A2R for a in ang]
    return mmul(mtrans(*pos), mmul(mrotz(rz), mmul(mroty(ry), mrotx(rx))))

def node_matrix(ev, pos, ang, scl):
    m = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    if not ev & 1: m = mmul(m, mtrans(*pos))
    if not ev & 2:
        rx, ry, rz = [a * A2R for a in ang]
        # njRotateXYZ / FUN_8c27a06c post-multiplies the current matrix one
        # axis at a time.  Normal objects take Z, Y, X; eval flag 0x20 takes
        # Y, X, Z.  Writing Rx*Ry*Rz here reversed the Dreamcast operation
        # and visibly twisted Beat's more heavily rotated arm.
        m = mmul(m, mmul(mroty(ry), mmul(mrotx(rx), mrotz(rz))) if ev & 0x20 else mmul(mrotz(rz), mmul(mroty(ry), mrotx(rx))))
    if not ev & 4: m = mmul(m, mscale(*scl))
    return m
def xform(m, p): return (m[0]*p[0]+m[1]*p[1]+m[2]*p[2]+m[3], m[4]*p[0]+m[5]*p[1]+m[6]*p[2]+m[7], m[8]*p[0]+m[9]*p[1]+m[10]*p[2]+m[11])
def xform_n(m, n):
    v = (m[0]*n[0]+m[1]*n[1]+m[2]*n[2], m[4]*n[0]+m[5]*n[1]+m[6]*n[2], m[8]*n[0]+m[9]*n[1]+m[10]*n[2]); l = math.sqrt(sum(c*c for c in v)) or 1.0
    return (v[0]/l, v[1]/l, v[2]/l)

def read_object(d, off, B):
    """NJS_OBJECT at absolute address off (file offset = off-B)"""
    o = off - B
    ev, model = struct.unpack_from("<II", d, o); pos = struct.unpack_from("<3f", d, o + 8); ang = struct.unpack_from("<3i", d, o + 20)
    scl = struct.unpack_from("<3f", d, o + 32); child, sib = struct.unpack_from("<II", d, o + 44)
    return dict(ev=ev, model=model, pos=pos, ang=ang, scl=scl, child=child, sib=sib, addr=off)

def walk_tree(d, root, B, parent_m, valid=None, root_sib=True):
    """yield (node, world_matrix) depth-first following child/sibling; skips pointers outside the buffer / not in `valid`.
    root_sib=False ignores the root's own sibling link (model tables chain their roots as siblings)"""
    stack = [(root, parent_m, root_sib)]
    while stack:
        a, pm, fs = stack.pop()
        if not a or a < B or a - B + 0x34 > len(d) or (valid is not None and a not in valid): continue
        n = read_object(d, a, B); m = mmul(pm, node_matrix(n["ev"], n["pos"], n["ang"], n["scl"]))
        yield n, m
        if n["sib"] and fs: stack.append((n["sib"], pm, True))
        if n["child"]: stack.append((n["child"], m, True))

# ---- JGS builder ----
LIGHT_DIR = (0.4, 0.8, 0.45); _l = math.sqrt(sum(c*c for c in LIGHT_DIR)); LIGHT_DIR = tuple(c/_l for c in LIGHT_DIR)
TOON = False   # JSR cel shading: two tones, the shadow tone is 0.55 of the lit one (measured on a DC frame: lime 160,224,0 vs 88,120,0)
BAKED_LIGHT = None  # opt-in source stage profile; legacy exports stay reproducible
def shade(col, nrm, diffuse, light=True):
    base = col if col is not None else diffuse
    if BAKED_LIGHT and light and nrm is not None and col is None:
        from baked_light import shade as bake
        return bake(BAKED_LIGHT, base, nrm, TOON)
    a, r, g, b = (base >> 24) & 255, (base >> 16) & 255, (base >> 8) & 255, base & 255
    if light and nrm is not None and col is None:
        d = nrm[0]*LIGHT_DIR[0] + nrm[1]*LIGHT_DIR[1] + nrm[2]*LIGHT_DIR[2]
        k = (1.0 if d > 0.15 else 0.55) if TOON else 0.55 + 0.45 * max(0.0, d); r, g, b = int(r*k), int(g*k), int(b*k)
    return (a << 24) | (b << 16) | (g << 8) | r

MAXEDGE = 0.0   # > 0: split triangles whose edges exceed this length (the PSP GE rejects triangles projecting beyond its ±4096 px guard band)
def _mid(a, b):
    ca, cb = a[2], b[2]; c = 0
    for sh in (0, 8, 16, 24): c |= ((((ca >> sh) & 255) + ((cb >> sh) & 255)) // 2) << sh
    return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, c, tuple((a[3][i] + b[3][i]) * 0.5 for i in range(3)))
def subdivide(vlist, il, maxedge):
    """vlist: [(u, v, colour, (x, y, z))], il: index list of triangles -> same, with long edges split at midpoints"""
    verts = list(vlist); out = []; work = [tuple(il[i:i+3]) for i in range(0, len(il) - 2, 3)]; me2 = maxedge * maxedge
    def d2(i, j):
        # Rank at the float32 precision emitted to the renderer, so invisible
        # host/PSP libm differences cannot change equal-edge subdivision.
        p = struct.unpack('<3f', struct.pack('<3f', *verts[i][3]))
        q = struct.unpack('<3f', struct.pack('<3f', *verts[j][3]))
        x, y, z = (p[k] - q[k] for k in range(3))
        return x*x + y*y + z*z
    mids = {}
    def mid(i, j):
        k = (min(i, j), max(i, j))
        if k not in mids: mids[k] = len(verts); verts.append(_mid(verts[i], verts[j]))
        return mids[k]
    guard = 0
    while work and guard < 400000:
        guard += 1
        a, b, c = work.pop()
        e = [(d2(a, b), 0), (d2(b, c), 1), (d2(c, a), 2)]; l, which = max(e)
        if l <= me2: out.append((a, b, c)); continue
        if which == 0: m = mid(a, b); work.append((a, m, c)); work.append((m, b, c))
        elif which == 1: m = mid(b, c); work.append((a, b, m)); work.append((a, m, c))
        else: m = mid(c, a); work.append((a, b, m)); work.append((m, b, c))
    return verts, [i for t in out for i in t]
class JgsBuilder:
    def __init__(self): self.objs = []; self.batches = []; self.verts = bytearray(); self.idx = bytearray(); self.nv = 0; self.ni = 0; self.used = set(); self.bmin = [1e9]*3; self.bmax = [-1e9]*3; self.vertex_stride = 0; self.light_records = bytearray()
    def add_model(self, d, model_addr, B, m, texmap, light=True, twosided=False, cache=None, bindm=None, bindm_inv=None, owner=None, source_material=False, source_environment=False, source_lighting=0, retain_lighting=None, post_transform=None, light_matrix=None):
        """model_addr: absolute NJS_CNK_MODEL address; texmap(slot)->texture index.
        cache: Ninja vertex cache shared by the nodes of one object tree (chunk strips may index vertices of earlier nodes);
        entries are stored in bind-pose world space (bindm) and fetched back into this node's space (bindm_inv)."""
        mo = model_addr - B
        if mo < 0 or mo + 24 > len(d): return
        vl, pl = struct.unpack_from("<II", d, mo)
        if not vl or not pl or vl < B or pl < B or vl - B >= len(d) or pl - B >= len(d): return
        try: verts = parse_vlist(d, vl - B); tris = parse_plist(d, pl - B, source_material, source_environment)
        except Exception: return
        owners = {}
        if cache is not None:
            for idx, (pos, nrm, col) in verts.items():
                wp, wn = xform(bindm, pos), xform_n(bindm, nrm) if nrm else None
                # JGC2 retains the node that submitted each cached vertex.
                # A child plist can join its own vertices to parent vertices;
                # Ninja does not re-transform those cached parent vertices.
                cache[idx] = (wp, wn, col, owner, pos, nrm) if owner is not None else (wp, wn, col)
                if owner is not None: owners[idx] = owner
            for idx in set(i for _, _, _, tri in tris for i, _ in tri):
                if idx not in verts and idx in cache:
                    rec = cache[idx]
                    if owner is not None and len(rec) == 6:
                        wp, wn, col, vo, vp, vn = rec; verts[idx] = (vp, vn, col); owners[idx] = vo
                    else:
                        wp, wn, col = rec[:3]; verts[idx] = (xform(bindm_inv, wp), xform_n(bindm_inv, wn) if wn else None, col)
        if owner is not None:
            for idx in verts: owners.setdefault(idx, owner)
        groups = collections.defaultdict(lambda: ({}, []))
        for texid, fl, diffuse, tri in tris:
            key = (0xffff if texid == 0xffff else texmap(texid), (1 if fl & 0x08 else 0) | (2 if (fl & 0x10) or twosided else 0) | (4 if texid == 0xffff else 0) | (8 if fl & 0x01 else 0) | (fl >> 8)); vd, il = groups[key]
            for idx, uv in tri:
                if idx not in verts: continue
                pos, nrm, col = verts[idx]; vo = owners.get(idx, owner); vk = (idx, round(uv[0], 4), round(uv[1], 4), diffuse, vo)
                if vk not in vd:
                    wp = xform(m, pos); wn = xform_n(light_matrix if light_matrix is not None else m, nrm) if nrm else None
                    # A static PLAY bake uses the owner's bind-pose normal,
                    # including vertices retained from another bone's cache.
                    # Geometry, cache ownership and animation remain local.
                    if BAKED_LIGHT and owner is not None and nrm and cache is not None:
                        wn = cache[idx][1]
                    lit = light and not (BAKED_LIGHT and fl & 1)
                    vd[vk] = (len(vd), (uv[0], uv[1], shade(col, wn, diffuse, lit), wp, vo, nrm, bool(nrm and col is None), shade(col, nrm, diffuse, False)))
                il.append(vd[vk][0])
        first = len(self.batches); pts = []
        for (tex, bflags), (vd, il) in sorted(groups.items()):
            if source_lighting:
                assert source_environment and owner is not None
                bflags |= source_lighting << 12
            if len(il) < 3 or len(vd) > 65535: continue
            vlist = [t[1] for t in sorted(vd.values(), key=lambda t: t[0])]
            if MAXEDGE > 0:
                if owner is not None:
                    raise ValueError('rigged subdivision requires preserving vertex owners/local normals')
                vlist, il = subdivide(vlist, il, MAXEDGE)
            # a batch holds at most 65535 vertices / 16-bit indices: chunk the triangle list when subdivision grew it
            for ci in range(0, len(il), 60000):
                chunk = il[ci:ci+60000]; remap = {}; cv = []; cil = []
                for i in chunk:
                    if i not in remap: remap[i] = len(cv); cv.append(vlist[i])
                    cil.append(remap[i])
                voff = self.nv
                for vv in cv:
                    u, v, c, wp = vv[:4]
                    # Correct placement without changing established subdivision,
                    # vertex colors, UV interpolation or batch/index ownership.
                    if post_transform is not None:
                        assert owner is None
                        wp = xform(post_transform, wp)
                    if retain_lighting is not None:
                        assert owner is not None and retain_lighting in (0,1,2)
                        self.light_records += struct.pack('<3fII',*(vv[5] or (0,0,0)),vv[7],retain_lighting | (0x100 if vv[6] else 0))
                    if owner is None:
                        if self.vertex_stride not in (0, 24): raise ValueError("mixed JGS/JGC vertex formats")
                        self.vertex_stride = 24; self.verts += struct.pack("<ffIfff", u, v, c, *wp)
                    else:
                        stride=40 if source_environment else 28
                        if self.vertex_stride not in (0, stride): raise ValueError("mixed JGS/JGC vertex formats")
                        self.vertex_stride = stride; self.verts += struct.pack("<ffIfffHH", u, v, c, *wp, vv[4], int(vv[6]) if source_lighting else 0)
                        if source_environment:
                            if bflags&2048 and vv[5] is None:raise ValueError('environment strip requires original vertex normals')
                            self.verts+=struct.pack('<3f',*(vv[5] or (0,0,0)))
                    pts.append(wp)
                ioff = self.ni; self.idx += struct.pack("<%dH" % len(cil), *cil); self.nv += len(cv); self.ni += len(cil)
                if tex != 0xffff: self.used.add(tex)
                self.batches.append([voff, ioff, len(cv), len(cil), tex, bflags])
                if self.ni & 1: self.idx += b"\0\0"; self.ni += 1
        if not pts: return
        cx = sum(p[0] for p in pts)/len(pts); cy = sum(p[1] for p in pts)/len(pts); cz = sum(p[2] for p in pts)/len(pts)
        r = math.sqrt(max((p[0]-cx)**2 + (p[1]-cy)**2 + (p[2]-cz)**2 for p in pts))
        for p in pts:
            for i in range(3): self.bmin[i] = min(self.bmin[i], p[i]); self.bmax[i] = max(self.bmax[i], p[i])
        self.objs.append(struct.pack("<ffffIIII", cx, cy, cz, r, first, len(self.batches) - first, 0, 0))

def swizzle8(data, w, h):
    out = bytearray(len(data)); rb = w // 16
    for y in range(h):
        by, yr = y // 8, y % 8
        for bx in range(rb):
            src = y * w + bx * 16; dst = (by * rb + bx) * 128 + yr * 16; out[dst:dst+16] = data[src:src+16]
    return bytes(out)

def write_jgs(path, bld, tex_imgs, texmax=128):
    """tex_imgs: list of PIL RGBA images indexed by texture id used in batches"""
    tex_recs = []; pix = bytearray(); tindex = {}
    def align(b, n=16):
        while len(b) % n: b.append(0)
    for t in sorted(bld.used):
        im = tex_imgs[t]; w, h = im.size
        while max(w, h) > texmax: w //= 2; h //= 2
        w = max(w, 16); h = max(h, 8); im = im.resize((w, h), Image.LANCZOS)
        has_alpha = im.getextrema()[3][0] < 255
        q = im.quantize(256, method=Image.Quantize.FASTOCTREE); pal = q.getpalette("RGBA") or q.getpalette()
        clut = bytearray()
        for i in range(256):
            if pal and (i*4+3) < len(pal): r, g, b, a = pal[i*4:i*4+4]
            elif pal and (i*3+2) < len(pal): r, g, b = pal[i*3:i*3+3]; a = 255
            else: r = g = b = a = 0
            clut += bytes((r, g, b, a))
        align(pix); oc = len(pix); pix += clut; align(pix); op = len(pix); pix += swizzle8(q.tobytes(), w, h)
        tindex[t] = len(tex_recs); tex_recs.append([w, h, 1 if has_alpha else 0, oc, op])
    batches = []
    for vo, io, nv, ni, tex, fl in bld.batches:
        ti = tindex.get(tex, 0)
        if tex != 0xffff and tex_recs[ti][2]: fl |= 1
        batches.append(struct.pack("<IIHHHH", vo, io, nv, ni, ti, fl))
    hdr = 4 + 5*4 + 6*4 + 6*4; off = (hdr + 15) & ~15; off_tex = off; off += 16*len(tex_recs)
    off = (off + 15) & ~15; off_obj = off; off += 32*len(bld.objs); off = (off + 15) & ~15; off_batch = off; off += 16*len(batches)
    off = (off + 15) & ~15; off_vert = off; off += len(bld.verts); off = (off + 15) & ~15; off_idx = off; off += len(bld.idx); off = (off + 15) & ~15; off_pix = off
    with open(path, "wb") as f:
        f.write(b"JGS1" + struct.pack("<5I", len(tex_recs), len(bld.objs), len(batches), bld.nv, bld.ni))
        f.write(struct.pack("<6I", off_tex, off_obj, off_batch, off_vert, off_idx, off_pix)); f.write(struct.pack("<6f", *bld.bmin, *bld.bmax))
        def pad(n):
            while f.tell() < n: f.write(b"\0")
        pad(off_tex)
        for w, h, fl, oc, op in tex_recs: f.write(struct.pack("<HHHHII", w, h, fl, 0, off_pix + oc, off_pix + op))
        pad(off_obj); f.write(b"".join(bld.objs)); pad(off_batch); f.write(b"".join(batches))
        pad(off_vert); f.write(bld.verts); pad(off_idx); f.write(bld.idx); pad(off_pix); f.write(pix)
    return len(bld.objs), len(batches), bld.nv, bld.ni // 3
