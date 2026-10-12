#!/usr/bin/env python3
"""Compare a disc-generated route to a private expected-only observation.

Reference observations are never passed to the original execution engine.
The independent hardware table used by that engine is not disc content.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import sys

def verify(raw_path, oracle, out):
    root=Path(__file__).resolve().parents[3]
    sys.path.insert(0,str(root/'decompile/tools'))
    from title_route_assets import bake
    from garam_audio_assets import capture_cues
    out.mkdir(parents=True,exist_ok=True)
    raw=raw_path.read_bytes()
    assert raw[:16]==struct.pack('<4s3I',b'TAC2',0x900,0x290,0xd0)
    assert (len(raw)-16)%4192==0
    (out/'title_actor_camera.bin').write_bytes(raw)
    with (out/'title.csv').open('w') as stream:
        writer=csv.writer(stream);writer.writerow(('character','player_clock'))
        for at in range(16,len(raw),4192):
            writer.writerow((struct.unpack_from('<I',raw,at+0x7c)[0],struct.unpack_from('<I',raw,at+0x98)[0]))
    actual=list(struct.iter_unpack('<6I',Path(str(raw_path)+'.cues').read_bytes()))
    with (out/'audio.csv').open('w') as stream:
        writer=csv.writer(stream);writer.writerow(('serial','tick','kind','bank','program','parent','task'))
        for i,row in enumerate(actual):writer.writerow((i,*row))
    with (oracle/'audio.csv').open() as stream:
        expected=[tuple(int(row[k]) for k in ('tick','kind','bank','program','parent','task')) for row in csv.DictReader(stream)]
    output,details=bake(out);reference,_=bake(oracle)
    (out/'route.bin').write_bytes(output)
    original=(oracle/'title_actor_camera.bin').read_bytes()
    samples=(len(raw)-16)//4192
    report={
        'complete_route':True,'samples':samples,'bytes':len(output),
        'route_byte_identical':output==reference,
        'route_sha256':hashlib.sha256(output).hexdigest(),
        'expected_sha256':hashlib.sha256(reference).hexdigest(),
        'actor_bytes_identical':len(raw)==len(original) and all(raw[at:at+0x900]==original[at:at+0x900] for at in range(16,len(raw),4192)),
        'cues':len(actual),'cue_timeline_identical':[r[:4] for r in actual]==[r[:4] for r in expected],
        'cue_payload_identical':capture_cues(out,out/'route.bin')==capture_cues(oracle,out/'route.bin'),
        'expected_observations_used_only_after_execution':True,
        'raw_capture_byte_identical':raw==original,
        'raw_capture_note':'Reference instrumentation changes camera callback addresses; unused allocation tails and temporary audio task addresses are not asset fields.',
    }
    (out/'verified.json').write_text(json.dumps(report,indent=2)+'\n')
    assert report['route_byte_identical'] and report['cue_timeline_identical'] and report['cue_payload_identical'],report
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('raw','oracle','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();print(json.dumps(verify(a.raw.resolve(),a.oracle.resolve(),a.out.resolve()),indent=2))
