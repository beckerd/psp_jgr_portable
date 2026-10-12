#!/usr/bin/env python3
"""Build PSP assets from a user's supported GDI in an isolated staging tree.

This development importer fails closed if any required asset cannot yet be
reproduced. It never reads the checkout's psp assets or emulator captures.
Only a complete, byte-verified conversion may publish data/assets.
"""
from __future__ import annotations
import argparse
import fnmatch
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zlib

from disc import Disc, DiscError, extract_afs, safe_name, sha256
from pipeline import CAPTURE_FAMILIES, graph, run, seed_sources
from semantic import cans_digest
from observations import jobs as observation_jobs, prepare as prepare_observations

ROOT=Path(__file__).resolve().parents[2]
DEFAULT_EXPECTED=Path(__file__).with_name('asset_catalog.json')
NON_ASSETS={'EBOOT.PBP','jgr_me.prx','jgr_crash.prx','crash_handler_license.txt'}

def catalog(path):
    data=json.loads(path.read_text());rows=data['files']
    result={}
    for r in rows:
        name=safe_name(r['path'])
        if name in NON_ASSETS:continue
        if name in result or r['bytes']<0 or len(r['sha256'])!=64:raise ValueError('Invalid expected asset catalog')
        result[name]=r
    return result

def inventory(folder,expected):
    rows=[]
    for name in sorted(expected):
        path=folder/name
        if not path.is_file():continue
        h=hashlib.sha256();crc=0;size=0
        with path.open('rb') as stream:
            for block in iter(lambda:stream.read(1024*1024),b''):
                size+=len(block);h.update(block);crc=zlib.crc32(block,crc)
        row=dict(path=name,bytes=size,sha256=h.hexdigest(),crc32=f'{crc:08x}')
        if expected[name].get('semantic_format')=='CAN3-opaque-source-identity-v1':
            row['semantic_sha256']=cans_digest(path.read_bytes())
        rows.append(row)
    return rows

def coverage(expected,rows):
    found={r['path']:r for r in rows};missing=[];different=[]
    for name,want in expected.items():
        if name not in found:missing.append(name)
        elif any(found[name][field]!=want[field] for field in ('bytes','sha256')):different.append(name)
    return sorted(missing),sorted(different)

def publish(source,out,rows,fingerprint):
    if out.exists():raise ValueError('Output already exists; choose a new pack directory')
    out.parent.mkdir(parents=True,exist_ok=True)
    temporary=out.with_name(out.name+'.partial')
    temporary.mkdir(exist_ok=False)
    try:
        for r in rows:
            dst=temporary/r['path'];dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/r['path'],dst)
            if dst.stat().st_size!=r['bytes'] or sha256(dst)!=r['sha256']:raise ValueError('Copied asset verification failed')
        manifest='JGR_ASSETS 1\nlayout 1\ncount '+str(len(rows))+'\n'
        manifest+=''.join(f'{r["bytes"]} {r["crc32"]} {r["path"]}\n' for r in rows)
        (temporary/'assets.manifest').write_text(manifest,encoding='ascii')
        (temporary/'source.json').write_text(json.dumps(fingerprint,indent=2)+'\n')
        (temporary/'catalog.json').write_text(json.dumps(dict(schema=1,source='user-disc',complete=True,files=rows),indent=2)+'\n')
        temporary.rename(out)
    except Exception:
        shutil.rmtree(temporary);raise

def prepare(gdi,work,repo,resume):
    with Disc(gdi) as disc:
        fingerprint=disc.fingerprint()
        marker=work/'disc.json'
        if work.exists():
            if not resume:raise ValueError('Work directory exists; use --resume after checking its disc fingerprint')
            if not marker.is_file() or json.loads(marker.read_text())!=fingerprint:raise ValueError('Work directory belongs to different/incomplete disc extraction')
        else:
            work.mkdir(parents=True)
            tree=work/'tree';(tree/'decompile').mkdir(parents=True)
            print('Extracting supported user disc',flush=True)
            files=disc.extract(tree/'decompile/extracted')
            (work/'disc_files.json').write_text(json.dumps(files,indent=2)+'\n')
            print('Unpacking original AFS archives',flush=True)
            for path in sorted((tree/'decompile/extracted').rglob('*')):
                if path.suffix.upper()=='.AFS':extract_afs(path,tree/'decompile/afs')
            marker.write_text(json.dumps(fingerprint,indent=2)+'\n')
    tree=work/'tree'
    if (tree/'decompile/tools').exists():shutil.rmtree(tree/'decompile/tools')
    seed_sources(repo,tree)
    raw=json.loads(Path(__file__).with_name('raw_assets.json').read_text())['files']
    for name,origin in raw.items():
        src=tree/safe_name(origin);dest=tree/'psp'/safe_name(name)
        if not src.is_file():raise ValueError('Missing original raw asset '+origin)
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
    return tree,fingerprint

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('gdi',type=Path);p.add_argument('--work',type=Path,required=True)
    p.add_argument('--out',type=Path,help='New complete data/assets directory; never publishes incomplete output')
    p.add_argument('--expected',type=Path,default=DEFAULT_EXPECTED)
    p.add_argument('--repo',type=Path,default=ROOT,help='Source-only tree containing decompile/tools and converter specs')
    p.add_argument('--capture-dir',type=Path,action='append',default=[],help='Completed fresh-disc capture with matching portable receipt; repeatable')
    p.add_argument('--capture-map',type=Path,help='JSON observation-key to fresh-capture directory map; every receipt is independently verified')
    p.add_argument('--cc',default='psp-gcc',help='PSP compiler for checked numeric blob ABI')
    p.add_argument('--reference-source',type=Path,help='Verified Flycast source checkout for SH-4/AICA hardware tables; required for a complete pack')
    p.add_argument('--emulator',type=Path,help='Lua-enabled Flycast executable for fresh playback observations')
    p.add_argument('--config',type=Path,help='Existing Flycast configuration; restored after capture')
    p.add_argument('--resume',action='store_true');p.add_argument('--only',nargs='+',help='Run graph targets and prerequisites (development; cannot publish)')
    p.add_argument('--extract-only',action='store_true',help='Verify disc and extract/copy original files only')
    a=p.parse_args();a.work=a.work.resolve();a.repo=a.repo.resolve()
    try:
        expected=catalog(a.expected);tree,fingerprint=prepare(a.gdi,a.work,a.repo,a.resume)
        declared={}
        if a.capture_map:
            declared=json.loads(a.capture_map.read_text())
            if not isinstance(declared,dict) or not all(isinstance(k,str) and isinstance(v,str) for k,v in declared.items()):
                raise ValueError('Capture map must contain observation keys and directory paths')
            declared={k:(a.capture_map.parent/Path(v)).resolve() for k,v in declared.items()}
        captures=prepare_observations(a.capture_dir+list(declared.values()),fingerprint,a.work,a.gdi,a.repo,a.emulator,a.config)
        if any(captures.get(k)!=v for k,v in declared.items()):raise ValueError('Capture map keys disagree with verified receipts')
        missing_observations=sorted({j['key'] for j in observation_jobs()}-set(captures))
        reference_source=a.reference_source.resolve() if a.reference_source else None
        nodes=graph(tree,captures,a.cc,a.repo/'psp/src',reference_source)
        results={} if a.extract_only else run(tree,a.work/'logs',nodes,a.only)
        rows=inventory(tree/'psp',expected)
        # Regenerate identical legacy/runtime aliases from a verified output.
        by_hash={r['sha256']:r['path'] for r in rows}
        names={r['path'] for r in rows}
        for name,want in expected.items():
            if name not in names and want['sha256'] in by_hash:
                dest=tree/'psp'/name;dest.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(tree/'psp'/by_hash[want['sha256']],dest)
        rows=inventory(tree/'psp',expected);missing,different=coverage(expected,rows)
        equivalent=[r['path'] for r in rows if r['path'] in different and r.get('semantic_sha256') and r['semantic_sha256']==expected[r['path']].get('semantic_sha256')]
        different=[n for n in different if n not in equivalent]
        capture_dependencies=[dict(path=n,converter=converter,trace=trace,status='capture_backend_required')
                  for n in missing for pattern,(converter,trace) in CAPTURE_FAMILIES.items() if fnmatch.fnmatchcase(n,pattern)]
        complete=not missing and not different and not missing_observations and not a.only and not a.extract_only and reference_source is not None and all(r['status']=='ok' for r in results.values())
        report=dict(schema=1,source='user-disc',complete=complete,disc=fingerprint,files=rows,bytes=sum(r['bytes'] for r in rows),
                    expected_count=len(expected),missing=missing,different=different,semantic_equivalent=equivalent,capture_dependencies=capture_dependencies,observations={k:str(v) for k,v in captures.items()},nodes=results,
                    generated_sources=[str(x.relative_to(tree)) for x in sorted((tree/'psp/src').rglob('*')) if x.is_file()])
        reference_receipt=tree/'reference_inputs/receipt.json'
        report['reference_source']=json.loads(reference_receipt.read_text()) if reference_source is not None and reference_receipt.is_file() else None
        report['missing_observations']=missing_observations
        raw_names=json.loads(Path(__file__).with_name('raw_assets.json').read_text())['files']
        report['coverage']=[dict(path=name,status='missing' if name in missing else 'different' if name in different else 'semantic-equivalent' if name in equivalent else 'verified',
                                 source='disc-copy' if name in raw_names else 'conversion') for name in sorted(expected)]
        (a.work/'conversion.json').write_text(json.dumps(report,indent=2)+'\n')
        print(f'{len(rows)-len(different)}/{len(expected)} assets match; {len(missing)} missing; {len(different)} differ',flush=True)
        print('Report: '+str(a.work/'conversion.json'),flush=True)
        if a.out and complete:publish(tree/'psp',a.out.resolve(),rows,fingerprint)
        if not complete:
            print('Asset setup is incomplete; no runtime manifest or playable pack was published.',file=sys.stderr)
            return 2
        return 0
    except (DiscError,ValueError,OSError,subprocess.CalledProcessError) as exc:
        print(f'Asset setup failed: {exc}',file=sys.stderr);return 1
if __name__=='__main__':raise SystemExit(main())
