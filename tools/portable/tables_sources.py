"""Additional native table recipes, expressed only as original source locations.

Coordinates mirror audio_mix_assets.py, audio_spatial_assets.py,
world_audio_assets.py, player_sound_assets.py, source_data_tables.py,
ending_assets.py and runtime_bindings.py. Output spans come from the checked
portable ABI declarations, not generated source values.
"""

WIDTH = {'b': 1, 'B': 1, 'h': 2, 'H': 2, 'i': 4, 'I': 4, 'f': 4}


def extra_recipes(groups, get_layout, repo):
    groups = {r['name']: r for r in groups}
    result = []

    def begin(name, source='decompile/extracted/1ST_READ.BIN', base=0x8c010000, special=0):
        row = dict(output='source_data_' + name + '.jsd', source=source, base=base,
                   sections=get_layout(groups[name]), fields=[], sort=0,
                   bgm=0, bgm_count=0, bgm_names=0, special=special,
                   version=2 if name.startswith('ptr_') else 1, fixups=0)
        result.append(row)
        return row

    def span(row, name, address, count, kind, output_kind=None, stride=None,
             output_stride=None, offset=0, pointer_add=None):
        section = next(s for s in row['sections'] if s['name'] == name)
        output_kind = output_kind or kind
        stride = stride or WIDTH.get(kind, 4)
        width = WIDTH[output_kind]
        output_stride = output_stride or width
        if offset + (count - 1) * output_stride + width > section['size']:
            raise ValueError('Source recipe exceeds output ABI: ' + name)
        for i in range(count):
            row['fields'].append(dict(address=address + i * stride,
                                      add=pointer_add or 0, destination=section['offset'] + offset + i * output_stride,
                                      kind=kind, output_kind=output_kind,
                                      flags=1 if pointer_add is not None else 0, precision=-1))

    row = begin('audio_mix_tables')
    span(row, 'AUDIO_MIX_TABLE', 0x8c1ccd04, 17 * 5, 'i')
    span(row, 'AUDIO_BGM_CLASS', 0x8c02eae8 + 4, 190, 'x', 'b', stride=4)

    row = begin('audio_spatial_tables', 'decompile/extracted/JETRADIO/MANATEE.DRV', base=0, special=1)
    for name, address, count in [('MANATEE_TL', 0x4f1c, 256), ('MANATEE_SEND', 0x501c, 64),
                                 ('MANATEE_PAN', 0x505c, 128), ('MANATEE_PAN_BASE', 0x50dc, 32)]:
        span(row, name, 32 + address, count, 'B')

    row = begin('world_audio_tables')
    span(row, 'WORLD_NPC_REACTION', 0x8c1b9652, 76, 'h', stride=8)
    span(row, 'WORLD_SEQUENCE_CHANNEL', 0x8c1d5960, 17, 'I', 'B', stride=28)

    row = begin('npc_audio_tables')
    for offset, kind in [(0, 'h'), (2, 'h'), (4, 'I')]:
        span(row, 'NPC_AUDIO_TYPES', 0x8c1b9650 + offset, 76, kind, stride=8, output_stride=8, offset=offset)
    for offset, kind in [(0, 'f'), (4, 'h'), (6, 'H')]:
        span(row, 'NPC_AUDIO_ROUTES', 0x8c1b98b0 + offset, 25, kind, stride=8, output_stride=8, offset=offset)

    row = begin('player_sound_tables')
    span(row, 'PLAYER_FOOT_CONTACT', 0x8c17e890, 140, 'B')
    span(row, 'PLAYER_SURFACE_SOUND', 0x8c1948f0, 64, 'i', 'b')

    row = begin('motmap')
    span(row, 'MOTMAP', 0x8c10a808, 112, 'i', 'h', stride=16)
    span(row, 'MOTMAP_ALT', 0x8c10a80c, 112, 'i', 'h', stride=16)

    row = begin('spraytab')
    span(row, 'CUES', 0x8c17e3f0, 11 * 7, 'i')
    span(row, 'PATROWS', 0x8c17e524, 21 * 4, 'i')

    row = begin('staff_draw')
    for vector in range(12):
        for component in range(8):
            span(row, 'STAFF_XY', 0x8c21bf80 + vector * 4, 1, 'f',
                 offset=(vector * 8 + component) * 4, pointer_add=component * 4)
    for name, address, count in [('STAFF_UV_PATTERN_X', 0x8c21bfb0, 4), ('STAFF_UV_PATTERN_Y', 0x8c21bff0, 4),
                                 ('STAFF_UV_PAGE_X', 0x8c21c030, 2), ('STAFF_UV_PAGE_Y', 0x8c21c050, 2)]:
        span(row, name, address, count * 8, 'H')

    row = begin('main_mark_model')
    span(row, 'MARK_MODEL', 0x8c10dd64, 24, 'm', 'B', stride=4)

    row = begin('scene_command_program')
    span(row, 'program', 0x8c1ce468, 8, 'c', 'i', stride=8)

    row = begin('ptr_audio_channel_tables')
    for bank, address, count in [(0, 0x8c1d2df0, 16), (2, 0x8c1d303c, 15), (4, 0x8c1d3384, 19),
                                  (5, 0x8c1d3598, 19), (6, 0x8c1d37ac, 16), (7, 0x8c1d396c, 16),
                                  (16, 0x8c1d5400, 49), (17, 0x8c1d595c, 17)]:
        span(row, 'AUDIO_CLASSES_' + str(bank), address + 4, count, 'I', 'B', stride=28)

    row = begin('ptr_mixed_bgm_c__bgm_name_0', special=2)
    row['fixups'] = 211
    row = begin('ptr_mixed_record_book_c__place_0', special=3)
    row['fixups'] = 5
    for name, special in [('main_enemy_maps', 4), ('main_remaining_enemy_maps', 5),
                          ('npc_squad_anim', 6), ('sniper_motion', 7), ('police_clips', 8),
                          ('onishima_primary', 9), ('onishima_overlay', 10)]:
        begin(name, special=special)
    row = begin('pause_wipe_path')
    span(row, 'PAUSE_WIPE_PATH', 0x8c17fdac, 32, 'f', stride=16, output_stride=8)
    span(row, 'PAUSE_WIPE_PATH', 0x8c17fdb0, 32, 'f', stride=16, output_stride=8, offset=4)
    for name, special in [('game_hud_entries', 11), ('message_hud_entries', 12),
                          ('trick_hud_entries', 13), ('spray_digits', 14),
                          ('pause_hud_entries', 15), ('practice_hud_entries', 16), ('ending_tables', 17)]:
        begin(name, special=special)
    for name, special, fixups in [('ptr_eventnames', 18, 112), ('ptr_hud_text', 19, 2),
                                  ('ptr_result_hud_text', 20, 34)]:
        row = begin(name, special=special)
        row['fixups'] = fixups
    import re
    import runpy
    tuning = runpy.run_path(str(repo / 'decompile/tools/player_tuning.py'))['FIELDS']
    declaration = (repo / 'psp/src/source_data_ptr_player_tuning.c').read_text().split('} CharTuning;')[0]
    declaration = declaration.split('typedef struct {', 1)[1]
    declaration = re.sub(r'/\*.*?\*/', '', declaration, flags=re.S)
    abi = []
    for statement in declaration.split(';'):
        if not statement.strip():
            continue
        kind, names = statement.strip().split(None, 1)
        for name in names.split(','):
            match = re.fullmatch(r'(\w+)(?:\[(\d+)\])?', name.strip())
            if not match or kind not in ('int', 'float'):
                raise ValueError('Unsupported player tuning ABI declaration')
            if match[2]:
                abi.extend((match[1] + '[' + str(i) + ']', kind[0]) for i in range(int(match[2])))
            else:
                abi.append((match[1], kind[0]))
    if abi != [(name, kind) for name, _, kind in tuning]:
        raise ValueError('Player tuning source field order does not match output ABI')
    row = begin('ptr_player_tuning')
    for char in range(16):
        for index, (_, offset, kind) in enumerate(tuning):
            span(row, 'PLAYER_TUNING', 0x8c182884 + char * 4, 1, kind,
                 offset=(char * len(tuning) + index) * 4, pointer_add=offset)
    for name, special, address, parameter in [('dog_source', 21, 0x8c1ba444, 'PAR_T2'),
                                              ('onishima_source', 22, 0x8c1b918c, 'PAR_ONISHIMA')]:
        row = begin(name, special=special)
        for i in range(24):
            span(row, parameter, address + i * 4, 1, 'I' if i < 3 or i >= 20 else 'f', 'f', offset=i * 4)
        if special == 21:
            span(row, 'DOG_CAPSULE', 0x8c1ba208, 7, 'f')
            span(row, 'DOG_RELEASE', 0x8c1ba4b8, 6, 'f')
            span(row, 'DOG_ATTACK', 0x8c1ba4a4, 3, 'I', 'i')
    for name, special, parameters, attacks in [
        ('ptr_killer_source', 23, [('PAR_T3', 0x8c1b7b44)], [('KILLER_ATTACK', 0x8c1b7ba4, 3)]),
        ('ptr_riot_source', 24, [('PAR_T4', 0x8c1b76f4)], [('RIOT_ATTACK', 0x8c1b7754, 2)]),
        ('ptr_para_source', 25, [('PAR_T7', 0x8c1b823c)], [('PARA_ATTACK', 0x8c1b829c, 9)]),
        ('ptr_bomb_source', 26, [('GANG_PAR_A', 0x8c1b7e0c), ('GANG_PAR_B', 0x8c1b7e80)],
         [('BOMB_ATTACK_A', 0x8c1b7e6c, 3), ('BOMB_ATTACK_B', 0x8c1b7ee0, 3)]),
        ('ptr_burning_source', 27, [('PAR_T6', 0x8c1b9b2c), ('PAR_T9', 0x8c1b887c), ('PAR_T25', 0x8c1b88e4)], []),
        ('ptr_taban_source', 28, [('PAR_TABAN', 0x8c21b524)], []),
    ]:
        row = begin(name, special=special)
        for parameter, address in parameters:
            for i in range(24):
                span(row, parameter, address + i * 4, 1, 'I' if i < 3 or i >= 20 else 'f', 'f', offset=i * 4)
        for attack, address, count in attacks:
            span(row, attack, address, count, 'I')
        if special == 27:
            span(row, 'KAIRIKI_RADIUS', 0x8c1b9b98, 1, 'f')
            for i in range(16):
                span(row, 'FIRE_IDLE_TARGETS', 0x8c1b8980 + i * 4, 1, 'I' if i % 4 == 3 else 'f', 'f', offset=i * 4)
    row = begin('ptr_taban_attack_source')
    span(row, 'TABAN_ANCHORS', 0x8c21b584, 6, 'f')
    for name, address, count in [('benten', 0x8c177d60, 4), ('kogane', 0x8c17b920, 14), ('shibuya', 0x8c1747c4, 4)]:
        # Same source spans as the three per-area passage decoders.
        row = begin(name + '_portals')
        for record in range(count):
            span(row, name + '_portals', address + record * 112, 24, 'f', offset=record * 96)
    begin('stage_exit_data', special=29)
    row = begin('kogane_water_data', special=30)
    for record, address in enumerate((0x8c17c4e0, 0x8c17c528, 0x8c17c554)):
        for i in range(9):
            span(row, 'KOGANE_WATER_RETURN', address + i * 4, 1, 'f' if i < 6 else 'i', offset=record * 36 + i * 4)
    span(row, 'KOGANE_WATER_HEIGHT', 0x8c1e10f8, 6, 'f')
    begin('final_emitter_texmap', special=31)
    row = begin('ptr_stage_camera')
    for offset in range(0, 216, 4):
        span(row, 'shibuya_passage_camera', 0x8c175a40 + offset, 1,
             'i' if offset in (0, 0xac, 0xb0, 0xb4, 0xc4, 0xd4) else 'f', offset=offset)
    row = begin('scene_tables', special=32)
    span(row, 'SCENE_CLIP', 0x8c10a808, 170, 'i', 'h', stride=16)
    begin('ptr_player_body', special=33)
    row = begin('ptr_player_motion', special=34)
    for index in range(4):
        span(row, 'PLAYER_ANIMATION_REFS', 0x8c10a808 + index * 4, 170, 'i', 'h', stride=16,
             output_stride=8, offset=index * 2)
    for index in range(2):
        span(row, 'LOCOMOTION_CLIPS', 0x8c10a808 + index * 4, 62, 'i', 'h', stride=16,
             output_stride=4, offset=index * 2)
    span(row, 'PLAYER_LOW_HEALTH', 0x8c182884, 16, 'I', 'i', pointer_add=0x1c)
    for index, address in enumerate((0x8c17e91c, 0x8c17e9a0)):
        span(row, 'MOTION_BLEND_CURVE', address, 11, 'f', stride=12, offset=index * 44)
    span(row, 'SPRAY_MOTION_MASK', 0x8c17e710, 11, 'I')
    for style in range(17):
        for motion in range(11):
            span(row, 'SPRAY_STYLE_MOTION', 0x8c17e6cc + style * 4, 1, 'I', 'B',
                 offset=style * 11 + motion, pointer_add=motion * 4)
    row = begin('ptr_trick_hud_tables', special=35)
    row['fixups'] = 93
    span(row, 'TRICK_POINTS', 0x8c183170, 20, 'i')
    span(row, 'TRICK_BONUS', 0x8c1831c0, 16, 'i')
    begin('ptr_story_intro_tables', special=36)
    row = begin('ptr_movie_tables', special=37)
    span(row, 'MOVIE_NEXT', 0x8c1db660 + 0x44, 35, 'i', 'b', stride=0x4c)
    span(row, 'MOVIE_FLAGS', 0x8c1db660 + 0x48, 35, 'I', 'B', stride=0x4c)
    row = begin('ptr_garage_departure', special=38)
    row['fixups'] = 11
    begin('rival_graffiti', special=39)
    begin('reverb_parameters', 'decompile/afs/E_SND_SE/079_test_fx.fpb', 0, special=40)
    begin('ptr_enemy_hardpoints', special=41)
    row = begin('player_effects', special=42)
    span(row, 'SKATE_DASH_CONTACT', 0x8c17f070, 56, 'B')
    begin('enemy_gas_body', special=43)
    row = begin('ptr_garage_cast', special=44)
    span(row, 'GARAGE_VOICE_PROGRAM', 0x8c10e0f0, 15, 'I')
    span(row, 'GARAGE_VOICE_TICK', 0x8c10e12c, 15, 'I')
    span(row, 'GARAGE_STOPS_0', 0x8c10dc70, 8, 'f')
    span(row, 'GARAGE_STOPS_1', 0x8c10dc90, 8, 'f')
    span(row, 'GARAGE_SELECT_FOV', 0x8c1057c8, 1, 'I')
    span(row, 'GARAGE_SPECIAL_CHAR', 0x8c21a790, 2, 'I', 'B')
    row = begin('ptr_garage_pending', special=45)
    row['fixups'] = 13
    row = begin('ptr_garage_ui', special=46)
    row['fixups'] = 61
    row = begin('ptr_pause_map_entries', special=47)
    row['fixups'] = 4
    begin('scene_dialogue_data', special=48)
    row = begin('bantam_train_data', special=49)
    span(row, 'bantam_train_lights', 0x8c1fbd34, 71*8, 'f')
    row = begin('benten_train_data', special=50)
    span(row, 'train_lights', 0x8c175d90, 46*8, 'f')
    row = begin('ptr_record_book', special=51)
    row['fixups'] = 68
    for name,address,count,kind in [
        ('RECORD_SCROLL',0x1b9ee4,5,'f'),('RECORD_PAGE',0x1b9ef8,5,'i'),
        ('RECORD_COLOR',0x1b9f20,6,'I'),('RECORD_PORTRAIT_XY',0x1b9f38,10,'f'),
        ('RECORD_JET_XY',0x103040,10,'f'),('RECORD_STORY_XY',0x103068,10,'f'),
        ('RECORD_SCORE_XY',0x103090,10,'f'),('RECORD_STORY_SCORE_XY',0x1030b8,2,'f'),
        ('RECORD_PTS_XY',0x1030c0,10,'f'),('RECORD_STORY_PTS_XY',0x1030e8,2,'f'),
        ('RECORD_SCORE_RIGHT',0x103120,5,'i'),('RECORD_JET_SCALE',0x103134,20,'f'),
        ('RECORD_STORY_SCALE',0x103184,4,'f')]:
        span(row,name,0x8c000000+address,count,kind)
    begin('enemy_audio_tables', special=52)
    begin('ptr_event_audio_tables', special=53)
    row = begin('ptr_scene_models', special=54)
    row['fixups'] = 170
    begin('graffiti_arrow_skater', special=55)
    row = begin('benten_opening_data', special=56)
    span(row, 'benten_lights', 0x8c175d90, 46*8, 'f')
    row = begin('bantam_opening_data', special=57)
    span(row, 'bantam_lights', 0x8c1fbd34, 71*8, 'f')
    row = begin('ptr_kogane_house_data', special=58)
    span(row, 'house_lod_distance2', 0x8c039140, 1, 'f')
    span(row, 'house_lights', 0x8c17c580, 114*8, 'f')
    row = begin('ptr_benten_crane_data', special=59)
    span(row, 'crane_lights', 0x8c175d90, 46*8, 'f')
    row = begin('ptr_street_props_data', special=60)
    row['fixups'] = 12
    begin('ptr_street_prop_audio', special=61)
    row = begin('kogane_cover_data', special=62)
    span(row, 'cover_lights', 0x8c17c580, 114*8, 'f')
    begin('ptr_grind_props_data', special=63)
    begin('shibuya_gate_data', special=64)
    begin('ptr_shibuya_door_data', special=65)
    begin('ptr_kogane_tunnel_materials', special=66)
    begin('stage_ped_proxy_data', special=67)
    row = begin('ptr_challenge_demo_data', special=68)
    row['fixups'] = 95
    begin('ptr_mission_cast', special=69)
    return result
