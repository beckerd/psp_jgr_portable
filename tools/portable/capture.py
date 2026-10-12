#!/usr/bin/env python3
"""Regenerate original playback observations from a fresh user-disc boot.

Requires a Lua-enabled Flycast supplied by the developer. This runner never
loads/saves a savestate or uses the development capture directory. Captured
outputs remain private game content. Completion does not imply byte parity;
the asset pipeline must compare converted output against its expected catalog.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import time

from disc import Disc

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = {
    'title': ('flycast_title_reference.lua', 'TITLE_REFERENCE complete'),
    'staff': ('flycast_staff_route_reference.lua', 'reason=staff_deleted'),
    'demo': ('flycast_challenge_demo.lua', 'CHALLENGE_REFERENCE complete samples='),
    'rival': ('flycast_challenge_rival.lua', 'CHALLENGE_RIVAL complete two sections and return-to-start'),
    'gang': ('flycast_poison_routes.lua', 'POISON_ROUTES complete all nine segments and return-to-start'),
}


def lua_string(value):
    # Paths/metadata here are ASCII; JSON string escaping is also valid Lua.
    return json.dumps(str(value), ensure_ascii=False)


def stage_scripts(repo, out, kind):
    tree = out / 'scripts'
    tools = tree / 'tools'
    tools.mkdir(parents=True, exist_ok=False)
    root_value = lua_string(str(tree.resolve()) + '/')
    for path in sorted((repo / 'decompile/tools').glob('flycast_*.lua')):
        content = path.read_text()
        content = re.sub(r"local root\s*=\s*['\"]/Users/[^\n]*?/JGR/decompile/['\"]", 'local root=' + root_value, content)
        if path.name in ('flycast_stage_reference.lua', 'flycast_ending_reference.lua'):
            content, count = re.subn(r'if frame==240 then flycast\.emulator\.loadState\(1\)\s*end',
                                    '-- Fresh boot: no private savestate.', content)
            if count != 1:
                raise ValueError('Unexpected capture boot hook: ' + path.name)
            content, count = re.subn(r'if frame==360 then',
                'if not portable_entered and frame>360 and task(6)~=0 and task(0x1b)~=0 then\n  portable_entered=true', content)
            if count != 1:
                raise ValueError('Unexpected capture entry hook: ' + path.name)
            content = 'local portable_entered=false\n' + content
        if path.name == 'flycast_staff_route_reference.lua':
            # A fresh boot briefly has the title's task 0x1b as well. The
            # credits lifecycle begins only after an actual STAFF sample.
            old = 'if staff then started=true;m.write32(staff_slot,staff)'
            if content.count(old) != 1:
                raise ValueError('Unexpected STAFF lifecycle hook')
            content = content.replace(old,
                'if staff then if installed and count>0 then started=true end;m.write32(staff_slot,staff)')
            # Do not write emulated executable RAM while REIOS is loading it.
            content = content.replace(' m.write16(0x8c0ee90c,0xe502)\n base();',
                ' if m.read16(0x8c01dc40)==0x2fe6 then m.write16(0x8c0ee90c,0xe502) end\n base();')
        if path.name == 'flycast_poison_routes.lua':
            phases=json.loads((repo/'tools/portable/gang_idle_phases.json').read_text())['phases']
            def lua_table(rows):
                return '{'+','.join('{'+','.join(map(str,row))+'}' for row in rows)+'}'
            table='{'+','.join('['+lua_string(key)+']='+lua_table(rows) for key,rows in phases.items())+'}'
            content+='''
-- A capture-only fixture makes the old entry blend independent of disc-load
-- duration. Native idle pose evaluation still runs; active route state and
-- one-tick arrival blends (zero entries) are untouched.
local portable_idle_phases='''+table+'''
local portable_gang_callback=flycast_callbacks.vblank
flycast_callbacks.vblank=function()
 portable_gang_callback()
 local phases=assert(portable_idle_phases[tostring(stage)..':'..os.getenv('JGR_REFERENCE_CHAPTER')])
 -- Preserve natural idle contacts while peers finish their authored route.
 -- At most the final two controller ticks may overlap entry-pose preparation;
 -- the native stream, rather than host frame timing, determines this boundary.
 local ending=true
 for _,a in ipairs(actors)do
  if m.read32(a.controller+0xb8)~=0 then
   local remaining=m.read32(a.controller+0x78)+1
   local record=m.read32(a.controller+0x70)+4
   while remaining<=2 and valid(record) and (m.read32(record)&0x80)==0 do
    remaining=remaining+(m.read32(record)&0xff)+1;record=record+4
   end
   if remaining>2 or not valid(record) then ending=false end
  end
 end
 for index,a in ipairs(actors)do
  local section=m.read32(a.controller+0xc0)
  local target=phases[index][section+1]
  if target and target>0 and m.read32(a.controller+0xb8)==0 and m.read32(a.p+0x78)==3
   and m.read32(a.dest+0xc48)==0 and m.read32(a.dest+0xc50)==section
   and ((phase<0 and section==0) or section==phase or (ending and section==phase+1)) then
   wf(a.p+0x5ac,target-1)
  end
 end
end
'''
        (tools / path.name).write_text(content)
    entry = out / 'capture.lua'
    if kind == 'title':
        (tools / SCRIPTS[kind][0]).write_text((repo / 'tools/portable/title_capture.lua').read_text())
    entry.write_text('''-- Fresh-disc capture: reject any accidentally retained state dependency.
local function forbidden() error('Portable extraction cannot load or save private states') end
flycast.emulator.loadState=forbidden
flycast.emulator.saveState=forbidden
local capture_stop={}
os.exit=function(status)
 local path=assert(os.getenv('JGR_REFERENCE_OUT'))..'capture.exit'
 local result=assert(io.open(path..'.tmp','w'))
 result:write(tostring(status or 0));result:close()
 assert(os.rename(path..'.tmp',path))
 flycast.emulator.pause()
 error(capture_stop)
end
''' + 'dofile(' + lua_string(tools / SCRIPTS[kind][0]) + ')\n' + '''
local callback=assert(flycast_callbacks.vblank)
flycast_callbacks.vblank=function()
 -- Optional local Flycast extension only changes host pacing. Every
 -- emulated tick and original game callback still runs and is verified.
 if flycast.emulator.setFastForward then flycast.emulator.setFastForward(true) end
 local ok,problem=xpcall(callback,function(problem)
  if problem==capture_stop then return capture_stop end
  return debug.traceback(tostring(problem))
 end)
 if not ok and problem~=capture_stop then
  local log=assert(io.open(assert(os.getenv('JGR_REFERENCE_OUT'))..'capture.error','w'))
  log:write(tostring(problem));log:close()
  print('PORTABLE_CAPTURE_ERROR '..tostring(problem));io.stdout:flush()
  os.exit(1)
 end
end
''')
    return entry


def run(args):
    if args.out.exists():
        raise ValueError('Capture destination must not exist')
    if not args.emulator.is_file() or not args.config.is_file():
        raise ValueError('Explicit Flycast executable and existing configuration are required')
    running = subprocess.run(['pgrep', '-x', args.emulator.name], capture_output=True)
    if running.returncode != 1:
        raise ValueError('Close the existing Flycast instance before capture')
    with Disc(args.gdi) as disc:
        fingerprint = disc.fingerprint()
    args.out.mkdir(parents=True)
    entry = stage_scripts(args.repo, args.out, args.kind)
    for directory in ('saves', 'vmu'):
        (args.out / directory).mkdir()
    env = dict({key: value for key, value in os.environ.items() if not key.startswith('JGR_')},
               JGR_REFERENCE_OUT=str(args.out.resolve()) + '/',
               JGR_REFERENCE_ROOT=str((args.out/'scripts').resolve())+'/',
               JGR_REFERENCE_STAGE=str(args.stage), JGR_REFERENCE_CHAPTER=str(args.chapter),
               JGR_REFERENCE_SCENE='25', JGR_REFERENCE_MAX_FRAMES='120000',
               JGR_REFERENCE_FALL='1', JGR_STAFF_CHARACTER=str(args.character),
               JGR_POISON_POSES='1', JGR_DEMO_CHARACTER=str(args.character),
               JGR_DEMO_RECORD=str(args.record), JGR_DEMO_SET=str(args.subset),
               JGR_REFERENCE_CHARACTER=str(args.player))
    options = {
        'config:LuaFileName': os.path.relpath(entry.resolve(), args.config.parent.resolve()),
        'config:Dynarec.Enabled': 'yes' if args.dynarec else 'no',
        'config:rend.ThreadedRendering': 'no', 'config:rend.vsync': 'no',
        'config:Debug.GDBEnabled': 'no', 'config:Dreamcast.AutoLoadState': 'no',
        'config:Dreamcast.AutoSaveState': 'no', 'config:VMUPath': str((args.out / 'vmu').resolve()),
        'config:SavePath': str((args.out / 'saves').resolve()), 'window:fullscreen': 'no',
    }
    # Flycast -config accepts quotes around paths containing commas/spaces.
    opts = ','.join(key + '=' + json.dumps(value) for key, value in options.items())
    original_config = args.config.read_bytes()
    process = None
    code = None
    try:
        for attempt in range(8):
            with (args.out / 'flycast.log').open('w') as log:
                process = subprocess.Popen([str(args.emulator), '-config', opts, str(args.gdi.resolve())],
                                           env=env, stdout=log, stderr=subprocess.STDOUT)
                started = time.monotonic()
                while process.poll() is None and not (args.out / 'capture.exit').is_file():
                    if time.monotonic() - started > args.timeout:
                        raise ValueError('Capture timed out; partial observations cannot be used')
                    time.sleep(0.2)
                marker = args.out / 'capture.exit'
                code = int(marker.read_text()) if marker.is_file() else process.returncode
                if marker.is_file():
                    break
            failure_log = (args.out / 'flycast.log').read_text(errors='replace')
            # This local Flycast build intermittently fails its address-space
            # assertion before Lua or game startup. Never retry a capture fault.
            if ('Verify Failed' not in failure_log or 'sq_buffer' not in failure_log or
                    (args.out / 'title_actor_camera.bin').exists()):
                break
            (args.out / ('startup-attempt-%d.log' % (attempt + 1))).write_text(failure_log)
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if args.config.read_bytes() != original_config:
            args.config.write_bytes(original_config)
    text = (args.out / 'flycast.log').read_text(errors='replace')
    marker = SCRIPTS[args.kind][1]
    complete = code == 0 and marker in text and not (args.out / 'capture.error').exists() and 'Lua exception' not in text
    if args.kind == 'staff':
        meta = args.out / 'staff_route_metadata.json'
        complete = complete and meta.is_file() and json.loads(meta.read_text()).get('complete', False)
    if args.kind in ('title', 'staff', 'demo'):
        path=args.out/'title_actor_camera.bin'
        complete=complete and path.is_file()
        if complete:
            with path.open('rb') as stream: header=stream.read(16)
            complete=header==struct.pack('<4s3I',b'TAC2',0x900,0x290,0xd0) and path.stat().st_size>16 and (path.stat().st_size-16)%4192==0
        if args.kind=='demo':complete=complete and (args.out/'audio.csv').is_file()
    else:
        for slot in range(3 if args.kind=='gang' else 1):
            path=args.out/('rival%d.bin'%slot)
            complete=complete and path.is_file()
            if complete:
                with path.open('rb') as stream: header=stream.read(8)
                complete=header==struct.pack('<4sI',b'PRT2',4192) and path.stat().st_size>8 and (path.stat().st_size-8)%4192==0
    # Converters additionally validate every actor tick against disc control
    # streams and compare final asset hashes. A capture receipt is not parity.
    receipt = {'schema': 1, 'kind': args.kind, 'disc': fingerprint, 'fresh_boot': True,
               'private_states_used': False, 'emulator_sha256': hashlib.sha256(args.emulator.read_bytes()).hexdigest(),
               'exit_code': code, 'capture_complete': complete, 'asset_parity_verified': False,
               'selection':dict(stage=args.stage,chapter=args.chapter,character=args.character,
                                player=args.player,record=args.record,subset=args.subset)}
    (args.out / 'capture.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if not complete:
        raise ValueError('Capture did not pass completion checks; inspect ' + str(args.out / 'flycast.log'))
    print('Completed fresh-disc observation: ' + str(args.out), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=sorted(SCRIPTS), required=True)
    parser.add_argument('--gdi', type=Path, required=True)
    parser.add_argument('--emulator', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--repo', type=Path, default=ROOT)
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--dynarec', action='store_true')
    parser.add_argument('--stage', type=int, default=1)
    parser.add_argument('--chapter', type=int, default=0)
    parser.add_argument('--character', type=int, choices=range(16), default=0)
    parser.add_argument('--player', type=int, choices=range(16), default=0)
    parser.add_argument('--record', type=int, default=0)
    parser.add_argument('--subset', type=int, default=0)
    args = parser.parse_args()
    try:
        run(args)
    except (ValueError, OSError) as exc:
        parser.exit(1, str(exc) + '\n')


if __name__ == '__main__':
    main()
