"""Audited original rigid street-prop dispatch, indexed by physical stage.

These are task placements, not every legacy layout category called DYN.
In particular Benten's 1ba9d0/1bc4cc is stationary decoration; its live
prop task records are 1ba918 and model/collider tables 1bc064/1bc294.
All addresses are from the US executable. Sounds are +bank16/-bank17.
"""

# Runtime variants retain each original task's launch/bounce/lifetime rules.
BOUNCE = 0                 # 0571e0 / 057280 / 0574c4
FIXED = 1                  # 058c60 / 058cdc / 058e90
SINGLE_BOUNCE = 2           # 0de360 / 0de3dc / 0de5cc
CHILD_YAW = 3               # 058690 / 0586d0 / 058858
SILENT_FIXED = 4            # 05a9e0 / 05ab1c, Benten second prop bank
FULL_BOUNCE = 5             # 0e9df0 / 0ea0b4, Kogane 59/60
HORIZONTAL_FIXED = 6        # 049f80 / 04a194, Kogane 37..41
CHILD_ROLL_BOUNCE = 7       # 0e1a28 / 0e1ce0, Kogane 30
CHILD_YAW_MOTIONS={51:0x8cda1164,52:0x8cda1498,53:0x8cda1934}

SHIBUYA_SOUNDS = {4:32,5:-8,10:32,12:31,13:32,14:33,15:28,
                  16:-8,17:-8,18:-8,19:-8,20:32,21:32,22:27,27:32}
KOGANE_BOUNCE = (2,6,10,11,13,14,16,22,23,24,25,26,27,28,29,34,43,61,64,66,67)
KOGANE_SINGLE_BOUNCE = (3,12,15,20,21,35,36,62,63,65,70,88,89,90)
BENTEN_FIXED = (1,2,3,4,5,9,10,12,13,14,15,16,17,18,19,20,21,24,25,26,
                27,28,29,30,31,35,36,37,38,39,40,41,42,43,45,49,54,55,56,
                67,68,69)
BANTAM_FIXED = (0,2,3,4,6,7,8,9,10,11,14,15,17,18,19)

def _types(sounds, reaction):
    return {t:dict(sound=sound,reaction=reaction,accepts=9 if reaction in (SINGLE_BOUNCE,FULL_BOUNCE) else 1)
            for t,sound in sounds.items()}

_kogane_sounds={t:31 for t in KOGANE_BOUNCE}
_kogane_sounds.update({23:30,24:30,25:30,26:30,61:28,67:27})
_kogane_types=_types(_kogane_sounds,BOUNCE)
_kogane_types.update(_types({t:27 if t in (3,12,20,21,90) else 31
                            for t in KOGANE_SINGLE_BOUNCE},SINGLE_BOUNCE))
_kogane_types.update(_types({59:31,60:31},FULL_BOUNCE))
_kogane_types.update(_types({t:28 for t in (37,38,39,40,41)},HORIZONTAL_FIXED))
_kogane_types.update(_types({30:28},CHILD_ROLL_BOUNCE))
_benten_sounds={t:31 for t in BENTEN_FIXED}
_benten_sounds.update({t:28 for t in (3,14,25,26,27,28,29,30,31,40,41,42,49,54,55,56)})
_benten_sounds.update({13:27,21:27,16:32,17:32,35:30,37:42,38:42,39:42})
_benten_types=_types(_benten_sounds,FIXED)
_benten_types.update(_types({51:28,52:28,53:28},CHILD_YAW))
_bantam_sounds={t:27 for t in BANTAM_FIXED}
_bantam_sounds.update({0:28,2:31,3:31,4:32,14:34,15:28,17:32})

STAGES={
    1:dict(name='Shibuya',tasktable=0x8c10618c,sectors=63,stride=40,typecount=28,
           modeltable=0x8c107564,collisiontable=0x8c107644,texlisttable=0x8c1075d4,
           spawn_handler=0x8c030c94,dispatch_handler=0x8c030688,
           types=_types(SHIBUYA_SOUNDS,BOUNCE),
           counts={4:1,12:3,13:15,14:9,15:7,16:23,17:24,18:24,19:23,20:6,21:7,22:2,27:11}),
    2:dict(name='Kogane',tasktable=0x8c108284,sectors=114,stride=40,typecount=91,
           modeltable=0x8c109d2c,collisiontable=0x8c10a2dc,texlisttable=0x8c109e98,
           # 03901c installs both model/texture tables. Each rigid helper
           # switches at camera distance squared >= literal039140 (160²),
           # before its idle/flying state branch.
           far_modeltable=0x8c10a004,far_texlisttable=0x8c10a170,lod_squared=25600.,
           spawn_handler=0x8c03901c,dispatch_handler=0x8c03861c,
           types=_kogane_types,
           counts={2:5,3:9,6:12,10:7,11:12,13:2,14:1,15:4,16:1,20:14,21:5,
                   23:8,24:10,25:10,26:9,27:15,28:10,29:17,30:1,34:4,35:3,36:5,
                   37:7,38:2,39:3,40:3,41:3,43:6,59:1,60:1,61:30,62:26,63:39,
                   64:7,65:3,66:119,67:48,70:40,88:51,89:3,90:5}),
    3:dict(name='Benten',tasktable=0x8c1ba918,sectors=46,stride=52,typecount=70,
           modeltable=0x8c1bc064,collisiontable=0x8c1bc294,texlisttable=0x8c1bc17c,
           spawn_handler=0x8c048ad8,dispatch_handler=0x8c048b64,
           types=_benten_types,
           counts={1:17,2:11,3:7,4:7,5:11,9:23,10:3,12:3,13:2,14:2,15:7,16:8,17:10,
                   18:1,21:4,24:6,25:2,26:1,27:1,28:1,29:2,30:3,31:1,35:2,36:1,
                   37:3,38:3,39:3,40:4,41:7,42:7,43:3,45:3,49:3,51:2,52:2,53:2,
                   55:2,56:3,67:2,68:1,69:2}),
    5:dict(name='Bantam Street',tasktable=0x8c203298,sectors=71,stride=52,typecount=20,
           modeltable=0x8c203f44,collisiontable=0x8c204084,texlisttable=0x8c203f94,
           spawn_handler=0x8c0f4c44,dispatch_handler=0x8c0f43c8,
           types=_types(_bantam_sounds,FIXED),
           counts={0:12,2:4,3:5,4:12,6:1,7:1,8:1,9:1,10:2,11:11,14:11,15:2,17:39,18:8}),
}
STAGES[3]['extra_banks']=[dict(name='Benten secondary props',tasktable=0x8c1baa88,
    sectors=46,stride=52,typecount=7,modeltable=0x8c1bc64c,
    collisiontable=0x8c1bc6a0,texlisttable=0x8c1bc668,
    spawn_handler=0x8c048f9c,dispatch_handler=0x8c048f9c,
    types=_types({0:0,1:0,2:0},SILENT_FIXED),counts={0:1,1:3,2:4})]

# Original modules with no rigid generic placements: Final Groove's pieces
# are bespoke STAGELAST tasks; Grind Square's 49 records have types0..34,
# all explicitly dispatched before the generic default of 0f5adc.
AUDITED_EMPTY_STAGES={4:'STAGELAST: bespoke boss/platform tasks, no street prop table',
                     6:'0f5adc: types0..34 all specialized; generic default has no placements'}

# These retain their dedicated authored animation/mission behavior, and must
# not silently enter a rigid prop path merely because they have a body box.
SPECIAL_FAMILIES={
    1:{'gates':(0,1),'doors':(2,3,6,7,8,9,11),'animated':(23,24,25,26)},
    2:{'animated/special':(0,1,4,5,7,8,9,17,18,19,42,58,76,85,86,87),
       'contact proxies':(31,32,33,44,45,46,47,48,49,50,51,52,53,54,55,56,57,
                          68,69,71,72,73,74,75,77,78,79,80,81,82,83,84)},
    3:{'global solids/openings':(0,6,23,46,47,48,65),
       'animated interaction':(7,8,11,22,32,33,34,44,50,66),
       'animated without collision shape':(60,61,62,63,64),
       'special actors':(57,58,59)},
    5:{'doors/windows':(1,5),'animated interaction':(12,13),'contact proxy':(16,)},
}
