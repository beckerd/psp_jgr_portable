#!/usr/bin/env python3
"""Preserve two documented historical shared-bank export conventions.

Reconstruct from current user-disc output; no binary payload is embedded.
The old raw-motion importer dropped scale keys whose relative source offset
was zero. This affects only unused helicopter motion 0, bone 1 (both variants);
flight callbacks use motions 5,9,6,7,8,10,11. Keep the tested bank semantics.
Untextured PatCar outline batches predate per-model material imports and retain
no blend encoding. Their outline flag makes those blend bits irrelevant.
The normal enemy LOD repacker later eliminates the now-unreferenced scale keys.
"""
import argparse
from pathlib import Path
import struct


def restore(data):
    if data[:4] != b'JGC2':
        raise ValueError('Expected JGC2 character bank')
    out = bytearray(data)
    h = struct.unpack_from('<16I', out, 4)
    if h[0] != 51:
        raise ValueError('Compatibility recipe must precede enemy LOD assembly')
    for model in (35, 37):
        first, count, motion, motions = struct.unpack_from('<4I', out, h[7] + model * 48)
        if count != 4 or motions != 12:
            raise ValueError('Unexpected helicopter skeleton/motion layout')
        frames, channel = struct.unpack_from('<2I', out, h[13] + motion * 8)
        at = channel + 24 + 16
        samples, offset = struct.unpack_from('<2I', out, at)
        if frames != 80 or samples != 80:
            raise ValueError('Unexpected legacy helicopter scale stream')
        struct.pack_into('<2I', out, at, 0, 0)
    first, count = struct.unpack_from('<2I', out, h[7] + 26 * 48)
    for bone in range(first, first + count):
        start, batches = struct.unpack_from('<2I', out, h[8] + bone * 48 + 40)
        for index in range(start, start + batches):
            at = h[9] + index * 16 + 14
            flags = struct.unpack_from('<H', out, at)[0]
            if flags & 4:
                struct.pack_into('<H', out, at, flags & 15)
    return bytes(out)


def finish(tree):
    import sys
    sys.path.insert(0,str(tree/'decompile/tools'))
    from jgc_lod import texture
    from pvr_decode import decode
    from ninjalib import Image,swizzle8
    bank=tree/'psp/chars.jgc';data=bank.read_bytes();h=struct.unpack_from('<16I',data,4)
    if h[0]!=68 or h[5]!=44:raise ValueError('Unexpected completed character layout')
    textures=[texture(data,i) for i in range(h[5])]
    # Historical PatCar import retained source TXR image1 (van) unused,
    # before its model-specific car/beacon material correction. Regenerate
    # that harmless legacy slot so the full bank remains byte reproducible.
    raw=(tree/'decompile/afs/ENEMY/020_patcartxr.bin').read_bytes();at=0;images=[]
    while True:
        at=raw.find(b'PVRT',at)
        if at<0:break
        length=struct.unpack_from('<I',raw,at+4)[0]
        images.append(decode(raw[at:at+8+length]));at+=8+length
    im=images[1];w,ht=im.size
    while max(w,ht)>256:w//=2;ht//=2
    w=max(w,16);ht=max(ht,8);im=im.resize((w,ht),Image.LANCZOS)
    q=im.quantize(256,method=Image.Quantize.FASTOCTREE);pal=q.getpalette('RGBA')
    unused=(w,ht,int(im.getextrema()[3][0]<255),0,bytes((pal+[0]*1024)[:1024]),swizzle8(q.tobytes(),w,ht))
    old_to_new={35:23,23:34,34:35};ordered=list(textures)
    for old,new in old_to_new.items():ordered[new]=textures[old]
    ordered[24]=unused
    out=bytearray(data[:h[15]])
    for i in range(h[2]):
        at=h[9]+i*16+12;ti,flags=struct.unpack_from('<2H',out,at)
        if not flags&4 and ti==24:raise ValueError('Legacy unused texture became referenced')
        struct.pack_into('<H',out,at,old_to_new.get(ti,ti))
    for i,t in enumerate(ordered):
        out.extend(bytes(-len(out)%64));clut=len(out);out.extend(t[4])
        out.extend(bytes(-len(out)%64));pixels=len(out);out.extend(t[5])
        struct.pack_into('<4H2I',out,h[12]+i*16,*t[:4],clut,pixels)
    bank.write_bytes(out)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bank', type=Path,nargs='?')
    parser.add_argument('--finish-tree',type=Path)
    args = parser.parse_args()
    if args.finish_tree:finish(args.finish_tree.resolve())
    elif args.bank:args.bank.write_bytes(restore(args.bank.read_bytes()))
    else:parser.error('bank or --finish-tree required')


if __name__ == '__main__':
    main()
