#!/usr/bin/env python3
"""Extract original PLAY attachment points and grind-foot masks; bake effect models.

No authored geometry/textures. Asset addresses verified in US Flycast RAM:
044498/044bec -> table slot 7, 044f78/045514 -> slot 6.
The DAT is linked at 8c8e1000 (not the EFTX header at 8c8e0000).
"""
from pathlib import Path
import struct
import subprocess
import sys
from ninjalib import read_object

ROOT = Path(__file__).resolve().parents[2]
DEC = ROOT / 'decompile'

def generate():
    exe = (DEC/'extracted/1ST_READ.BIN').read_bytes()
    masks = struct.unpack_from('<15I', exe, 0x17f130-0x10000)
    lines = ['/* Original PLAY section 5 anchors; 0b71ec -> player+684. */',
             'static const SkateAnchor SKATE_ANCHORS[16][29] = {']
    maps = []
    for row in (DEC/'chars_spec.txt').read_text().splitlines()[:16]:
        name, source, *_ = row.split(':')
        d = (DEC/source).read_bytes()
        u = lambda o: struct.unpack_from('<I', d, o)[0]
        secs = {}; o = u(4)
        while u(o): secs.setdefault(u(o), u(o+4)); o += 8
        nodes = []; anchors = {}
        def walk(a, parent=-1):
            if not a: return
            bi = len(nodes); nodes.append((a, parent))
            if u(a+8):
                arr, n = struct.unpack_from('<2I', d, u(a+8))
                for k in range(n):
                    x,y,z,idx = struct.unpack_from('<3fI', d, arr+k*16)
                    assert idx not in anchors and idx < 29
                    anchors[idx] = (bi,x,y,z)
            walk(u(a+60),bi); walk(u(a+64),parent)
        walk(secs[5]); assert len(nodes)==20 and len(anchors)==29, name
        # Collision and display hierarchies share motion channel order.
        display=[]
        def visible(a,parent=-1):
            if not a:return
            bi=len(display);display.append((a,parent))
            visible(u(a+44),bi);visible(u(a+48),parent)
        visible(secs[1]); assert [p for a,p in nodes]==[p for a,p in display]
        lines.append('    { /* '+name+' */')
        for k in range(29):
            bi,x,y,z=anchors[k]
            lines.append('        {'+str(bi)+',{'+','.join(v.hex()+'f' for v in (x,y,z))+'}},')
        lines.append('    },')
        # 0c7d14/0c7d2c resolve the two six-entry arrays in section 4.
        values=struct.unpack_from('<12I',d,secs[4]+12)
        assert all(v<len(masks) for v in values), (name,values)
        maps.append([[masks[v] for v in values[k:k+6]] for k in (0,6)])
    lines += ['};','static const unsigned char SKATE_GRIND_MASK[16][2][6] = {']
    for row in maps:lines.append('    {'+','.join('{'+','.join(map(str,r))+'}' for r in row)+'},')
    lines += ['};','static const unsigned char SKATE_DASH_CONTACT[2][28] = {']
    contact=exe[0x17f070-0x10000:0x17f070-0x10000+56]
    for k in (0,28):lines.append('    {'+','.join(map(str,contact[k:k+28]))+'},')
    return '\n'.join(lines+['};',''])

def bake(out=None, spray=False):
    dat='afs/PLAYER/016_player_effect_dat.bin';txr='afs/PLAYER/017_player_effect_txr.bin'
    data=(DEC/dat).read_bytes();base=0x8c8e1000;rows=[]
    models=[('PaintSpray',0x8c8e3d2c,0x8c8eb1c8)] if spray else [('SkateSpark',0x8c8e6e14,0x8c8eb500),('WallStreak',0x8c8e17d8,0x8c8ead08),
        ('ShallowSplash',0x8c8e44e4,0x8c8ef8f4)] # 046b44: original effect table slot 0x98, 15 frames
    for name,root,motion in models:
        stack=[root];blend=set()
        while stack:
            node=read_object(data,stack.pop(),base)
            stack.extend(a for a in (node['child'],node['sib']) if a)
            if not node['model']:continue
            plist=struct.unpack_from('<I',data,node['model']-base+4)[0]-base
            # These six effect models each start with a diffuse/specular
            # material chunk. Its high byte carries source/dest blend factors.
            assert data[plist]==0x13
            blend.add((data[plist+1]>>3&7,data[plist+1]&7))
        assert len(blend)==1,(name,blend)
        src,dst=blend.pop()
        # 0446d0 calls motion draw mode 1 -> 234fc6 -> 23471e, not the
        # skater's outline renderer. Its 0x0b40 strip does NOT select inverse
        # face culling. Flycast's original draw header is 828c0002/c8800000/
        # 8491a4ed: ISP CullMode=1 (small triangles, neither face direction).
        # Preserve source geometry, colours and keys; explicitly keep both
        # orientations. Separately captured WallStreak mode 0 -> 294322 has
        # the same ISP c8800000 (opaque 808c0002/9489a4ed, fading tail
        # 828c0002/9491a4ed). It also does not reject either face direction.
        # PaintSpray mode 2 -> 235a5e was independently captured in the
        # original car lesson: 828c002a/c8800000/8491a4ed, also neither face.
        cull=':cull=none'
        rows.append(f'{name}:{dat}:{txr}:0,0,0,0:rawat:{base:x}:{root:x}:{motion:x}:blend={src},{dst}{cull}')
    subprocess.run([sys.executable,'tools/jgc_convert.py',str(out or ROOT/'psp/skatefx.jgc'),
                    '--nolight=1',*rows],cwd=DEC,check=True)

def trail_assets():
    """Two atlases, eight unresized original 64-square PTEX trail images each."""
    from pvr_decode import decode
    atlases=[bytearray(256*128*2) for _ in range(2)]
    for model,row in enumerate((DEC/'chars_spec.txt').read_text().splitlines()[:16]):
        name,_,path,*_=row.split(':');b=(DEC/path).read_bytes()
        record=struct.unpack_from('<I',b,8)[0]
        texlist=struct.unpack_from('<I',b,record+0x44)[0]
        _,count,off=struct.unpack_from('<3I',b,texlist);assert count==1
        at=b.index(b'PVRT',off,off+128);size=struct.unpack_from('<I',b,at+4)[0]
        im=decode(b[at:at+8+size]).convert('RGBA');assert im.size==(64,64)
        for y in range(64):
            for x in range(64):
                r,g,blue,a=im.getpixel((x,y))
                value=(r>>4)|(g>>4)<<4|(blue>>4)<<8|(a>>4)<<12
                target=((model%8//4*64+y)*256+model%4*64+x)*2
                struct.pack_into('<H',atlases[model//8],target,value)
    return struct.pack('<4sI',b'TEX1',2)+b''.join(struct.pack('<HH',256,128)+b for b in atlases)

if __name__=='__main__':
    result=generate();path=ROOT/'psp/src/player_effects.inc'
    if '--check' in sys.argv:
        assert path.read_text()==result
        print('All 16 PLAY attachment sets and original grind-foot masks verified')
    elif '--bake-trails' in sys.argv:(ROOT/'psp/skatetrails.bin').write_bytes(trail_assets())
    elif '--bake-spray' in sys.argv:bake(ROOT/'psp/paintspray.jgc',spray=True)
    elif '--bake' in sys.argv:bake()
    else: print(result,end='')
