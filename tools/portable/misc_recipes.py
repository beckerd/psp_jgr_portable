"""Native miscellaneous source formats; no converted data in this catalog."""
def recipes():
    names=('gamehud.bin','spraydigits.bin','tabanfx.bin','failurehud.bin','policehud.bin',
           'finaltagmask.bin','finalcoll.bin','finalbosscoll.bin','finalcollapse.bin','garageshadow.bin','skatetrails.bin','enemyaudio.bin')
    names+=tuple(f'rivaltag{i}.bin' for i in range(5))
    names+=('source_data_player_fsca.jsd','source_data_smsb_tables.jsd')
    return {name:(70,'decompile/extracted/1ST_READ.BIN',(0,0,0,0)) for name in names}
