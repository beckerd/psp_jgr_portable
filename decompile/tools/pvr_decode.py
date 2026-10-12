#!/usr/bin/env python3
"""Decode Dreamcast PVR (GBIX/PVRT) textures to PNG.
usage: pvr_decode.py OUTDIR file [file...]   (scans each file for embedded PVRT chunks)
Supports RGB565/ARGB1555/ARGB4444 in TWID, TWID_MIP, VQ, VQ_MIP, SMALLVQ(_MIP), RECT, RECT_STRIDE, TWID_RECT."""
import os, struct, sys
from PIL import Image

def twiddle(x, y):
    i = 0
    for b in range(10):
        i |= ((y >> b) & 1) << (2 * b)
        i |= ((x >> b) & 1) << (2 * b + 1)
    return i

def px(pf, v):
    if pf == 1:   # RGB565
        return ((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255
    if pf == 0:   # ARGB1555
        return ((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255 if v & 0x8000 else 0
    if pf == 2:   # ARGB4444
        return ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 15) * 17
    raise ValueError(f"pixfmt {pf}")

def decode(chunk):
    """chunk starts at 'PVRT'. returns PIL Image."""
    L, pf, df, _, w, h = struct.unpack_from("<IBBHHH", chunk, 4)
    payload = chunk[16:8 + L]
    img = Image.new("RGBA", (w, h))
    put = img.putpixel
    u16 = lambda buf, i: struct.unpack_from("<H", buf, i * 2)[0]
    def mip_skip(bytes_per_level):
        # mip chains are stored smallest-first; the 1x1 level occupies a minimum unit
        return sum(max(bytes_per_level(s), 1) for s in (1 << i for i in range(20)) if s < w)
    if df in (1, 2, 0x12):                 # square twiddled (+mips, smallest first; 1x1 padded to 4 bytes)
        off = (mip_skip(lambda s: s * s * 2) + 2) if df != 1 else 0
        lvl = payload[off:off + w * h * 2]
        for y in range(h):
            for x in range(w):
                put((x, y), px(pf, u16(lvl, twiddle(x, y))))
    elif df in (3, 4, 0x10, 0x11):         # VQ / small VQ (+mips)
        if df in (3, 4):
            entries = 256
        elif df == 0x10:
            entries = 16 if w <= 16 else 32 if w <= 32 else 128 if w <= 64 else 256
        else:
            entries = 16 if w <= 16 else 64 if w <= 32 else 256
        cb = payload[:entries * 8]
        off = entries * 8 + (mip_skip(lambda s: (s * s) // 4) if df in (4, 0x11) else 0)
        idx = payload[off:off + (w // 2) * (h // 2)]
        for by in range(h // 2):
            for bx in range(w // 2):
                e = idx[twiddle(bx, by)]
                for x2 in range(2):
                    for y2 in range(2):
                        put((bx * 2 + x2, by * 2 + y2), px(pf, u16(cb, e * 4 + x2 * 2 + y2)))
    elif df in (9, 0xB):                    # rectangle, linear
        for y in range(h):
            for x in range(w):
                put((x, y), px(pf, u16(payload, y * w + x)))
    elif df == 0xD:                         # rectangle of twiddled square tiles
        s = min(w, h)
        tiles_x = w // s
        for ty in range(h // s):
            for tx in range(tiles_x):
                base = (ty * tiles_x + tx) * s * s
                for y in range(s):
                    for x in range(s):
                        put((tx * s + x, ty * s + y), px(pf, u16(payload, base + twiddle(x, y))))
    else:
        raise ValueError(f"datafmt {df:#x}")
    return img

def main():
    out = sys.argv[1]; os.makedirs(out, exist_ok=True)
    n = 0
    for path in sys.argv[2:]:
        b = open(path, "rb").read(); i = 0; k = 0
        stem = os.path.basename(path)
        while True:
            i = b.find(b"PVRT", i)
            if i < 0: break
            L = struct.unpack_from("<I", b, i + 4)[0]
            try:
                img = decode(b[i:i + 8 + L])
                img.save(os.path.join(out, f"{stem}_{k:03d}.png")); n += 1
            except Exception as e:
                print(f"skip {stem}#{k} @{i:#x}: {e}")
            k += 1; i += 8 + max(L, 16)
    print(f"wrote {n} PNGs to {out}")

if __name__ == "__main__":
    main()
