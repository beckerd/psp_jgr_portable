"""Replay selectors and authored capture inputs; no original sampled content.

The gang spectator positions are test inputs retained from flycast_poison_routes.lua.
Idle clocks synchronize the port's established entry-blend fixture; neither is
an original rival route, pose, control stream or executable payload.
"""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
APPROACHES={
 'gang_poison':((226.43286,-415,619.736),(-39,-325,920),(-20,-320,1540)),
 'gang_love':((-400,493,-1380),(-174,207,-515),(-195,100,-130)),
 'gang_noise':((-80,94.5,-699),(-528,104.5,-1072),(618,74.5,-1071)),
 'gang_mask':((1340,0.6806788444519043,-720),(970,-70.1910629272461,-950),(1340,-70.19103240966797,-1050)),
}
def jobs():
    return json.loads(Path(__file__).with_name('capture_jobs.json').read_text())['jobs']
def outputs(job):
    kind=job['kind']
    if kind=='title':return ['title_route.bin']
    if kind=='staff':return [f"staffroute{job['character']:02d}.bin",f"staffroute{job['character']:02d}_meta.bin"]
    if kind=='demo':return [job['path']]
    if kind=='rival':return ['challenge_rival_'+job['mission'].lower()+'.bin']
    if kind=='gang':return [f"{job['prefix']}_rival{i}.bin" for i in range(3)]
    raise ValueError('Unknown replay kind')
def recipes():
    names=[name for j in jobs() for name in outputs(j)]
    names.append('source_data_ptr_challenge_demo_data.jsd')
    return {name:(91,'decompile/extracted/1ST_READ.BIN',(0,0,0,0)) for name in names}
def source():
    phases=json.loads(Path(__file__).with_name('gang_idle_phases.json').read_text())['phases']
    kind_ids={'demo':0,'title':1,'staff':2,'rival':3,'gang':4}
    lines=['/* Generated from replay selectors and authored spectator input fixtures. No observations. */',
           'static const ReplayJob replay_jobs[]={']
    demo=0
    for job in jobs():
        kind=job['kind'];names=outputs(job);names+=['']*(3-len(names))
        # STAFF metadata occupies session slot3; the public filename map below
        # retains two names. Title's baseline skater is an explicit port choice.
        selection=[job.get('stage',1),job.get('chapter',0),job.get('record',0),job.get('subset',0),job.get('player',0) if kind in ('rival','gang') else job.get('character',0),kind_ids[kind]]
        approach=APPROACHES.get(job['key'],((0,0,0),)*3)
        idle=phases[f"{job['stage']}:{job['chapter']}"] if kind=='gang' else ((0,0,0),)*3
        array=lambda rows,fmt:'{'+','.join('{'+','.join(fmt(v) for v in row)+'}' for row in rows)+'}'
        lines.append(' {{'+','.join(map(str,selection))+'},{'+','.join(json.dumps(n) for n in names)+'},'+str(job.get('character',0))+','+str(demo if kind=='demo' else -1)+',{'+array(approach,lambda v:float(v).hex()+'f')+','+array(idle,str)+'}},')
        demo+=kind=='demo'
    lines+=['};','']
    return '\n'.join(lines)
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');a=p.parse_args()
    target=ROOT/'psp/src/portable_replay_session_data.inc';text=source()
    if a.check:assert target.read_text()==text
    else:target.write_text(text)
