"""Source coordinates and format choices for native resource conversion."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]

def recipes():
    r={
        'fontmain.bin':(40,'decompile/afs/E_FONTMAIN/000_e_fontmaintxr.bin',(0,0,0,0)),
        'fontwide.bin':(40,'decompile/afs/E_FONTMAIN/000_e_fontmaintxr.bin',(1,0,0,0)),
        'spraycam.bin':(42,'decompile/afs/PLAYER/016_player_effect_dat.bin',(0,0,0,0)),
        'voices.bin':(44,'decompile/extracted/1ST_READ.BIN',(0,0,0,0)),
        'practicehud_list.bin':(45,'decompile/afs/E_TUTORIAL/000_tutorialdat.bin',(0,0,0,0)),
        'radio_names.txt':(46,'decompile/afs/U_GARAGE/001_garagedat.bin',(0x8c800000,0x350,26,0x354)),
        'grindmap.bin':(47,'decompile/afs/U_GARAGE/001_garagedat.bin',(0,0,0,0)),
        'grf_names.txt':(46,'decompile/afs/E_MARKEDIT/001_markeditdat.bin',(0x8c900000,4,102,0)),
        'grf_desc.txt':(46,'decompile/afs/E_MARKEDIT/001_markeditdat.bin',(0x8c900000,8,204,0)),
    }
    # Original character-to-source mapping already used by result_camera_assets.
    models=(4,6,3,2,1,7,10,0,15,8,13,11,12,5,9,14)
    lines=(ROOT/'decompile/chars_spec.txt').read_text().splitlines()
    for character,model in enumerate(models):
        source='decompile/'+lines[model].split(':')[1]
        r[f'resultcam{character:02d}.bin']=(41,source,(character,0,0,0))
    for stage,table in {1:0x8cddf04c,2:0x8cd9b4c8,3:0x8cb21d58,5:0x8cb0cdb0,6:0x8cb09708}.items():
        r[f'spots{stage}.bin']=(43,f'decompile/afs/STAGE{stage}/000_st{stage}dat.bin',(table,0,0,0))
    return r
