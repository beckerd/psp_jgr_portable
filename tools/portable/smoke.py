#!/usr/bin/env python3
"""Isolated portable-layout PPSSPP loading/render checks, not campaign parity.

Assets are linked read-only from a PRIVATE test installation. Each case owns its
cache, logs and saves. Missing/corrupt installs get their own asset directories.
PPSSPP settings saving is disabled before the appended configuration is loaded.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
PIXELS = 2 * 512 * 272 * 4
MISSIONS = {1: 'S1M01', 2: 'S2M01', 3: 'S3M01', 4: 'S4M01', 5: 'S5M01', 6: 'S6M01'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_runtime(runtime, stage):
    # Release builds emit the loading outcome, not per-frame profile rows.
    loaded = bool(re.search(rf'^LEVEL_READY stage={stage} error=0 ', runtime, re.M))
    campaign = loaded and bool(re.search(rf'^CAMPAIGN begin name={MISSIONS[stage]} ', runtime, re.M))
    city = loaded if stage == 4 else bool(re.search(rf'^STAGE_LOAD stage={stage} local=0', runtime, re.M))
    return city, campaign


def setup_failure_status(log):
    matches = re.findall(r'^SETUP status=(-?\d+) file=', log, re.M)
    return int(matches[-1]) if matches else None


def convert(raw, output):
    pixels = raw.read_bytes()
    if len(pixels) != PIXELS:
        raise ValueError('Incomplete capture: ' + str(raw))
    colors = []
    for i in range(2):
        frame = Image.frombytes('RGBA', (512, 272), pixels[i * PIXELS // 2:(i + 1) * PIXELS // 2])
        frame = frame.convert('RGB').crop((0, 0, 480, 272))
        frame.save(output.with_name(output.stem + f'_{i}.png'))
        colors.append(len(set(frame.getdata())))
    return colors


def clone(source, destination, case, seal):
    destination.mkdir(parents=True, exist_ok=False)
    # The production EBOOT must supply its own game and helper modules.
    shutil.copy2(source / 'EBOOT.PBP', destination / 'EBOOT.PBP')
    for name in ('cache', 'log'):
        (destination / 'data' / name).mkdir(parents=True)
    background = source / 'data/setup/background.jbg'
    if background.is_file():
        target = destination / 'data/setup/background.jbg'
        target.parent.mkdir(parents=True)
        shutil.copy2(background, target)
    if case == 'missing':
        return
    if case == 'corrupt':
        assets = destination / 'data/assets'
        assets.mkdir()
        for path in (source / 'data/assets').rglob('*'):
            if not path.is_file():
                continue
            target = assets / path.relative_to(source / 'data/assets')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.symlink_to(path.resolve())
        first = (assets / 'assets-manifest.txt').read_text().splitlines()[3].split(' ', 2)[2]
        changed = assets / first
        original = changed.read_bytes()
        changed.unlink()
        changed.write_bytes(bytes([original[0] ^ 1]) + original[1:])
    else:
        (destination / 'data/assets').symlink_to((source / 'data/assets').resolve(), target_is_directory=True)
        if seal and seal.is_file():
            shutil.copy2(seal, destination / 'data/cache/assets.checked')


def run_case(args, case, seal):
    out = args.out / case
    out.mkdir(parents=True, exist_ok=False)
    game = out / 'JGRVIEW'
    clone(args.install, game, case, seal)
    cache, logs = game / 'data/cache', game / 'data/log'
    failure = case in ('missing', 'corrupt')
    stage = int(case[5:]) if case.startswith('stage') else None
    frame = 1300 if stage in (4, 6) else 400
    shot = cache / ('shot2.raw' if frame == 1300 else 'shot.raw')
    if not failure:
        (cache / 'autopilot').write_text('neutral' + (' ' + MISSIONS[stage] if stage else '') + '\n')
        if case == 'title':
            shot = cache / 'shot_title.raw'
            (cache / 'logfixture').write_text('650\n')
        elif case == 'garage':
            (cache / 'garagetest').write_text('1\n')
            shot = cache / 'shot_garage.raw'
        else:
            (cache / 'logfixture').write_text(str(frame + 300) + '\n')
    config = out / 'settings.ini'
    config.write_text('[General]\nMemStickDirectory=' + str(out / 'config/ppsspp') + '\n'
                      'FirstRun=False\nAutoRun=True\nEnable Logging=True\n'
                      'PauseOnLostFocus=False\nPauseWhenMinimized=False\n'
                      'RemoteDebuggerOnStartup=True\nRemoteDebuggerLocal=True\n'
                      f'RemoteISOPort={args.port}\n'
                      '[Graphics]\nGraphicsBackend=0\nInternalResolution=1\n'
                      f'SoftwareRenderer={"True" if args.software else "False"}\nFrameSkip=0\nFullScreen=False\nVSync=False\n'
                      '[Sound]\nEnable=False\n[CPU]\nCPUCore=1\nCPUSpeed=333\n'
                      '[SystemParam]\nPSPModel=0\n')
    command = [str(args.emulator), '-j', str(game / 'EBOOT.PBP'),
               '--appendconfig=' + str(config), '--windowed', '--graphics=' + ('software' if args.software else 'vulkan'), '--loglevel=2']
    ready, capture, boot_seconds, error, gpu_capture = False, None, None, None, False
    setup_status = None
    start = time.monotonic()
    log = out / 'emulator.log'
    with log.open('wb') as stream:
        environment = dict(os.environ, SDL_AUDIODRIVER='dummy', XDG_CONFIG_HOME=str(out / 'config'))
        proc = subprocess.Popen(command, cwd=game, stdout=stream, stderr=subprocess.STDOUT, env=environment)
        try:
            while time.monotonic() - start < args.timeout and proc.poll() is None:
                runtime_path = logs / 'jgr_log.txt'
                runtime = runtime_path.read_text(errors='replace') if runtime_path.exists() else ''
                setup_log = logs / 'setup.log'
                if failure and setup_log.exists():
                    setup_status = setup_failure_status(setup_log.read_text(errors='replace'))
                if runtime and boot_seconds is None:
                    boot_seconds = time.monotonic() - start
                if not failure and seal and not runtime and time.monotonic() - start > args.cached_boot_timeout:
                    error = 'cached installation did not enter game startup before timeout'
                    break
                if failure and setup_status is not None and time.monotonic() - start > args.failure_wait:
                    screenshot = out / 'setup.raw'
                    captured = subprocess.run(['node', str(ROOT / 'tools/portable/sdk_screen.mjs'),
                                               str(args.port), str(screenshot)], capture_output=True, text=True, timeout=15)
                    (out / 'capture.txt').write_text(captured.stdout + captured.stderr)
                    if captured.returncode:
                        error = 'setup screenshot failed: ' + captured.stderr.strip()
                    else:
                        image = Image.frombytes('RGBA', (512, 272), screenshot.read_bytes()).convert('RGB').crop((0, 0, 480, 272))
                        image.save(out / 'setup.png')
                        capture = 'setup.png'
                        ready = screenshot.is_file() and not runtime_path.exists() and not (cache / 'assets.checked').exists()
                        if setup_status != -3:
                            error = 'setup stopped before missing-disc recovery (status %d)' % setup_status
                    break
                if not failure and shot.is_file() and shot.stat().st_size == PIXELS:
                    if not gpu_capture and not args.software:
                        gpu = subprocess.run(['node', str(ROOT / 'decompile/tools/ppsspp_gpu_capture.mjs'),
                                              str(args.port), str(out / 'game.png')], capture_output=True, text=True, timeout=15)
                        (out / 'capture.txt').write_text(gpu.stdout + gpu.stderr)
                        if gpu.returncode:
                            error = 'game screenshot failed: ' + gpu.stderr.strip()
                            break
                        gpu_capture = True
                    # The existing garage fixture loops in the garage branch;
                    # city/title fixtures leave through the ordinary exit path.
                    if case == 'garage' or ('LOG_FIXTURE exit_frame=' in runtime and 'ME_EXIT fenced;' in runtime):
                        ready = True
                        break
                if re.search(r'^STAGE_LOAD stage=\d+ local=-[2-9]\d*|^LEVEL_READY .* error=[1-9]|^MOVIE_LOAD_ERROR ', runtime, re.M):
                    error = 'game reported a loading error'
                    break
                time.sleep(.25)
        except Exception as exc:
            error = str(exc)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
    runtime_path = logs / 'jgr_log.txt'
    runtime = runtime_path.read_text(errors='replace') if runtime_path.exists() else ''
    if runtime_path.exists():
        shutil.copy2(runtime_path, out / 'runtime.log')
    if failure and not capture and not error:
        error = 'setup did not reach its missing-disc recovery screen'
    result = dict(case=case, passed=False, ready=ready, error=error,
                  seconds=time.monotonic() - start, boot_seconds=boot_seconds,
                  eboot_sha256=digest(game / 'EBOOT.PBP'), sdk_setup_capture=capture, emulator_exit_code=proc.returncode,
                  host_audio_output=False,
                  debugger_capture_disabled=args.software,
                  saves=[str(p.relative_to(out / 'config/ppsspp')) for p in
                         (out / 'config/ppsspp/PSP/SAVEDATA/JGR_PORTABLE').rglob('*') if p.is_file()],
                  root_runtime_files=[p.name for p in game.iterdir() if p.name.startswith(('jgr_log', 'jgr_story', 'shot'))],
                  cache_seal=(cache / 'assets.checked').is_file(),
                  setup_failure_status=setup_status,
                  checks='loading/render/input fixtures only; no earned mission or physical PSP performance claim')
    if shot.is_file():
        result['frame_colors'] = convert(shot, out / 'frame.png')
    if gpu_capture:
        result['frame_colors'] = [len(set(Image.open(out / 'game.png').convert('RGB').getdata()))]
        result['frame_source'] = 'PPSSPP rendered display or current render target; see capture.txt'
    if ready and not error and not result['saves'] and not result['root_runtime_files']:
        if failure:
            result['passed'] = True  # screenshot text must additionally be reviewed by a human/agent
            result['needs_setup_text_review'] = True
        elif stage:
            result['stage_load_ok'], result['campaign_ok'] = validate_runtime(runtime, stage)
            result['passed'] = result['stage_load_ok'] and result['campaign_ok'] and max(result.get('frame_colors', [0])) > 16
        else:
            result['passed'] = max(result.get('frame_colors', [0])) > 16
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('case', 'passed', 'ready', 'seconds', 'boot_seconds', 'error')}), flush=True)
    return result, cache / 'assets.checked'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--emulator', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', nargs='+', default=['title', 'stage1', 'stage2', 'stage3', 'stage4', 'stage5', 'stage6', 'garage', 'missing', 'corrupt'])
    parser.add_argument('--timeout', type=float, default=420)
    parser.add_argument('--failure-wait', type=float, default=15)
    parser.add_argument('--port', type=int, default=52937)
    parser.add_argument('--software', action='store_true', help='use CPU-visible game frame captures, with no gameplay debugger connection')
    parser.add_argument('--seal', type=Path, help='previous PSP-generated validation seal for these same immutable asset files')
    parser.add_argument('--cached-boot-timeout', type=float, default=30)
    args = parser.parse_args()
    args.install, args.emulator, args.out = args.install.resolve(), args.emulator.resolve(), args.out.resolve()
    if set(args.cases) - {'title', 'garage', 'missing', 'corrupt', *(f'stage{i}' for i in range(1, 7))}:
        parser.error('unknown case')
    args.out.mkdir(parents=True, exist_ok=True)
    seal, results = args.seal, []
    for case in args.cases:
        for attempt in range(3):
            result, candidate = run_case(args, case, seal)
            if result['passed'] or result['boot_seconds'] is not None or case in ('missing', 'corrupt') or attempt == 2:
                break
            # A host renderer can stall before EBOOT startup on this macOS
            # build. Preserve the failed attempt; never retry a gameplay error.
            (args.out / case).rename(args.out / f'{case}_startup_attempt{attempt + 1}')
        if candidate.is_file():
            seal = candidate
        results.append(result)
        (args.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        if not result['passed']:
            raise SystemExit('Portable smoke failed: ' + case)


if __name__ == '__main__':
    main()
