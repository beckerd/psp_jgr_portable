-- Original player challenges selected from the audited mission/set inventory.
JGR_CHALLENGE_REFERENCE=true
local root=assert(os.getenv('JGR_REFERENCE_ROOT'),'Use tools/portable/capture.py')
JGR_DEMO_REFERENCE={label='CHALLENGE',character=assert(tonumber(os.getenv('JGR_DEMO_CHARACTER'))),
 record=assert(tonumber(os.getenv('JGR_DEMO_RECORD'))),set=assert(tonumber(os.getenv('JGR_DEMO_SET')))}
dofile(root..'tools/'..(os.getenv('JGR_DEMO_NO_AUDIO')=='1' and 'flycast_recruit_demo_reference.lua' or 'flycast_recruit_demo_audio.lua'))
