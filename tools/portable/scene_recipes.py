"""Original file locations for native scene archives and staff cards."""
def recipes():
    executable='decompile/extracted/1ST_READ.BIN'
    out={name:(op,executable,(0,0,0,0)) for op,name in
         ((80,'scenes.bin'),(81,'cutscene_sources.bin'),(82,'events.txt'))}
    for index in range(20):
        out[f'staff{index:02d}.bin']=(2,f'decompile/afs/U_GRF/{107+index:03d}_staff{index+1:02d}.bin',(512,0,0,0))
    return out
