"""Accept only completed fresh-disc observations from the portable runner.

Capture receipts do not replace final asset validation. Every converted route
still must match the independently reviewed asset catalog before publication.
"""
import json
from pathlib import Path
import subprocess
import sys

def jobs():
    return json.loads(Path(__file__).with_name('capture_jobs.json').read_text())['jobs']


def validate(directory, fingerprint):
    receipt = json.loads((directory / 'capture.json').read_text())
    if (receipt.get('schema') != 1 or receipt.get('disc') != fingerprint or
            receipt.get('fresh_boot') is not True or receipt.get('private_states_used') is not False or
            receipt.get('capture_complete') is not True or receipt.get('exit_code') != 0):
        raise ValueError('Unverified/incomplete/different-disc capture: ' + str(directory))
    binary = receipt.get('emulator_sha256', '')
    if len(binary) != 64 or any(c not in '0123456789abcdef' for c in binary):
        raise ValueError('Missing emulator identity in capture receipt')
    kind = receipt.get('kind')
    if kind == 'title':
        key = 'title'
    elif kind == 'staff':
        meta = json.loads((directory / 'staff_route_metadata.json').read_text())
        character = meta.get('character')
        if not isinstance(character, int) or not 0 <= character < 16 or meta.get('complete') is not True:
            raise ValueError('Incomplete selected-character staff performance')
        key = 'staff%02d' % character
    elif kind in ('demo','rival','gang'):
        selection=receipt.get('selection',{})
        candidates=[job for job in jobs() if job['kind']==kind and
                    all(selection.get(field)==job.get(field,0) for field in
                        ('stage','chapter','character','player','record','subset'))]
        if len(candidates)!=1:
            raise ValueError('Unknown or ambiguous performance selection')
        key=candidates[0]['key']
    else:
        raise ValueError('Observation family not yet qualified for portable import: ' + str(kind))
    names=(['title_actor_camera.bin','title.csv'] if kind in ('title','staff','demo') else
           ['rival%d.bin'%n for n in range(3 if kind=='gang' else 1)])
    if kind=='demo':names.append('audio.csv')
    for name in names:
        if not (directory / name).is_file():
            raise ValueError('Missing observation input ' + name)
    return key, directory.resolve()


def prepare(directories, fingerprint, work, gdi, repo, emulator=None, config=None):
    if bool(emulator) != bool(config):
        raise ValueError('--emulator and --config must be supplied together')
    result = {}
    for directory in directories:
        key, path = validate(directory, fingerprint)
        if key in result:
            raise ValueError('Duplicate observation ' + key)
        result[key] = path
    if emulator:
        for job in jobs():
            key,kind=job['key'],job['kind']
            if key in result:
                continue
            out = work / 'observations' / key
            if out.exists():
                actual, path = validate(out, fingerprint)
                if actual != key:
                    raise ValueError('Wrong cached observation selector')
                result[key] = path
                continue
            command = [sys.executable, str(Path(__file__).with_name('capture.py')), '--kind', kind,
                       '--gdi', str(gdi.resolve()), '--repo', str(repo.resolve()), '--emulator', str(emulator.resolve()),
                       '--config', str(config.resolve()), '--out', str(out), '--dynarec']
            for field in ('stage','chapter','character','player','record','subset'):
                if field in job:command += ['--'+field,str(job[field])]
            subprocess.run(command, check=True)
            actual, path = validate(out, fingerprint)
            if actual != key:
                raise ValueError('Capture returned wrong performance selector')
            result[key] = path
    return result
