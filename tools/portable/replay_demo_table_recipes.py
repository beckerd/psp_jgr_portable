"""Port demo selectors and output labels; never captured actor/audio values."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def emit():
    jobs=[r for r in json.loads((ROOT/'tools/portable/capture_jobs.json').read_text())['jobs'] if r['kind']=='demo']
    if len(jobs)!=19:raise ValueError('Demo ABI requires19 selected performances')
    lines=['/* Port selector/filename metadata. No observed original timeline. */','static const DemoSelector demo_selectors[]={']
    for r in jobs:
        strings=[r[k] for k in ('mission','path','label')]
        if any(not s.isascii() or any(c in s for c in '\\\"\n\r') for s in strings):raise ValueError('Invalid selectorlabel')
        lines.append(' {%s,%du,%du},'%(','.join(json.dumps(s) for s in strings),r['subset'],r['character']))
    return '\n'.join(lines+['};',''])
if __name__=='__main__':(ROOT/'psp/src/portable_replay_demo_table_data.inc').write_text(emit())
