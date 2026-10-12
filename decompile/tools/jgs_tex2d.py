#!/usr/bin/env python3
"""PVR texture pack (GBIX/PVRT chunks) -> psp 'TEX1' file: u32 n; per texture {u16 w, u16 h, RGBA4444 pixels row-major}."""
import sys,struct; sys.path.insert(0, __file__.rsplit("/",1)[0])
from pvr_decode import decode as pvr_decode
MAX=int([a for a in sys.argv if a.startswith("--max=")][0][6:]) if any(a.startswith("--max=") for a in sys.argv) else 512
args=[a for a in sys.argv[1:] if not a.startswith("--")]
b=open(args[0],"rb").read(); i=0; imgs=[]
while True:
    i=b.find(b"GBIX",i)
    if i<0: break
    j=b.find(b"PVRT",i+8,i+64); L=struct.unpack_from("<I",b,j+4)[0]
    im=pvr_decode(b[j:j+8+L]).convert("RGBA")
    while max(im.size)>MAX: im=im.resize((im.size[0]//2,im.size[1]//2))
    imgs.append(im); i+=8
with open(args[1],"wb") as f:
    f.write(struct.pack("<4sI",b"TEX1",len(imgs)))
    for im in imgs:
        w,h=im.size; f.write(struct.pack("<HH",w,h)); px=im.tobytes(); out=bytearray(w*h*2)
        for k in range(w*h):
            r,g,b_,a=px[k*4:k*4+4]; struct.pack_into("<H",out,k*2,(a>>4)<<12|(b_>>4)<<8|(g>>4)<<4|(r>>4))
        f.write(out)
print(args[1],[im.size for im in imgs])
