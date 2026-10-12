"""Original source paths and parser choices for native reveal geometry."""
def recipes():
    out={'objectgraffiti.bin':(84,'decompile/extracted/1ST_READ.BIN',(0,0,0,0)),
         'finalgraffiti.bin':(87,'decompile/afs/STAGELAST/001_lastdat.bin',(0,0,0,0))}
    for stage in (1,2,3,5,6):
        out[f'walltags{stage}.bin']=(86,f'decompile/afs/STAGE{stage}/000_st{stage}dat.bin',(stage,0,0,0))
    for character in range(16):
        out[f'skatergraffiti{character:02d}.bin']=(85,'decompile/extracted/1ST_READ.BIN',(character,0,0,0))
    return out
