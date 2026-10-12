#!/usr/bin/env python3
"""Build an offline browser preparer from the shared C conversion sources.

Requires Emscripten 6.0.12 and Python 3.10+ on the developer's computer only.
The emitted HTML embeds its worker and WebAssembly. No game disc, assets or
EBOOT is read by this build; the user supplies the matching release at runtime.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
from build_device_setup import setup_sources, NATIVE_CORE
import notices
from source_bundle import SourceRelease, content_fingerprint, dependency_paths

WRAPS = ('fopen', 'stat', 'lstat', 'mkdir', 'remove', 'unlink', 'rename',
         'opendir', 'readdir', 'closedir', 'fileno', 'fstat', 'getcwd', 'chdir',
         'realpath', 'access', 'open', 'read', 'write', 'lseek', 'close')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emcc', type=Path, default=ROOT/'build/browser/emsdk/upstream/emscripten/emcc.py')
    parser.add_argument('--psp', type=Path, default=ROOT/'psp')
    parser.add_argument('--out', type=Path, default=ROOT/'build/browser/preparer')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--source-base-url', help='HTTPS release download directory for the separate matching source ZIP; omit for an offline package')
    args = parser.parse_args()
    emcc = args.emcc.resolve()
    compiler = ([sys.executable, str(emcc)] if emcc.suffix == '.py' else [str(emcc)])
    version = subprocess.check_output(compiler+['--version'], text=True).splitlines()[0]
    if ' 6.0.12 ' not in version:
        raise SystemExit('Use the pinned Emscripten 6.0.12 SDK; got '+version)
    psp = args.psp.resolve()
    release = SourceRelease(ROOT, psp/'src/portable_asset_identity.h', version, args.debug)
    notice_text, notice_files = notices.collect(release.files)
    notice_html = notices.render(notice_text, release.metadata, args.source_base_url)
    frontend = lambda name: release.files['tools/portable/browser/' + name].decode('utf-8')
    markers = ('<!-- JGR_PREPARE_STYLE -->', '<!-- JGR_PREPARE_WORKER -->',
               '<!-- JGR_PREPARE_SCRIPT -->', notices.MARKER)
    notices.replace_markers(frontend('prepare.html'), {marker: '' for marker in markers})
    output = args.out.resolve(); output.mkdir(parents=True, exist_ok=True)
    objects = output/'objects'; objects.mkdir(exist_ok=True)
    names, closure, tables = setup_sources(psp)
    excluded = {'portable_launcher.c', 'portable_setup_ui.c', 'crash.c', 'release_report.c'}
    names = [name for name in names if name not in excluded]
    native = set(NATIVE_CORE) | set(tables) | {name for name in names if name.startswith('portable_native_')}
    flags = ['-O2', '-std=gnu99', '-ffp-contract=off', '-fno-fast-math', '-D_GNU_SOURCE',
             '-DJGR_DIAGNOSTICS=0', '-DJGR_PORTABLE_ASSETS=1', '-DJGR_DEVICE_SETUP=0',
             '-DJGR_SETUP_ME=0', '-DJGR_BUNDLED_RUNTIME=1', '-DJGR_NATIVE_REPLAY=1',
             '-DJGR_SETUP_CRASH=0', '-I'+str(psp/'src'),
             '-idirafter', str(ROOT/'psp/tests/render_stubs'), '-idirafter', str(ROOT/'psp/tests/stubs'),
             '-include', str(psp/'src/portable_assets.h')]
    if args.debug: flags += ['-g3']
    sources = [(psp/'src'/name, name in native) for name in names]
    sources += [(HERE/name, False) for name in ('prepare.c', 'io.c', 'math.c')]
    # crash_build.inc is generated for the PSP diagnostic object, which this
    # browser build excludes. Any *active* dependency must still bind below.
    common_inputs = release.bind([path for path, _ in sources] +
                 [psp/'src'/name for name in closure if name != 'crash_build.inc'] +
                 list((ROOT/'psp/tests/render_stubs').glob('*.h')) +
                 list((ROOT/'psp/tests/stubs').glob('*.h')), psp)
    def compile_one(pair):
        source, is_native = pair
        obj = objects/(source.stem+'.o')
        command = compiler+flags
        if is_native:
            command += ['-DJGR_PORTABLE_ASSETS_IMPLEMENTATION=1', '-include', str(psp/'src/portable_native_namespace.h')]
        if source.parent == HERE:
            command += ['-DJGR_PORTABLE_ASSETS_IMPLEMENTATION=1']
        if source.name == 'ctrl.c':
            # Match the PSP libm when expanding recorded controller input.
            # Other converter math keeps its existing implementation.
            command += ['-Dsinf=jgr_browser_sinf', '-Dcosf=jgr_browser_cosf']
        command += ['-MMD', '-MF', str(obj.with_suffix('.d')), '-c', str(source), '-o', str(obj)]
        cached = obj.with_suffix('.command.json')
        input_cache = obj.with_suffix('.inputs.sha256')
        signature = json.dumps(command)
        def input_signature():
            files = dict(common_inputs)
            if obj.with_suffix('.d').exists():
                files.update(release.bind(dependency_paths(obj), psp))
            return content_fingerprint(files)
        current_inputs = input_signature()
        if (not obj.exists() or not cached.exists() or cached.read_text() != signature or
                not obj.with_suffix('.d').exists() or not input_cache.exists() or
                input_cache.read_text() != current_inputs):
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if result.returncode:
                print(result.stdout, file=sys.stderr)
                raise RuntimeError('Compilation failed: '+source.name)
            if result.stdout: print(result.stdout, end='')
            cached.write_text(signature)
            input_cache.write_text(input_signature())
        return obj
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        compiled = list(pool.map(compile_one, sources))
    dependency_count = release.bind_dependencies(compiled, psp)
    js = output/'converter.js'
    link = compiler+['-O2', '-ffp-contract=off']+[str(path) for path in compiled]+[
        '--no-entry', '-lm', '-sMODULARIZE=1', '-sEXPORT_NAME=createJgrPrepareModule',
        '-sENVIRONMENT=worker', '-sSINGLE_FILE=1', '-sSINGLE_FILE_BINARY_ENCODE=0',
        '-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=67108864', '-sMAXIMUM_MEMORY=1073741824',
        '-sSTACK_SIZE=2097152', '-sASYNCIFY=1', '-sASYNCIFY_STACK_SIZE=1048576',
        '-sEXPORTED_FUNCTIONS=["_jgr_browser_prepare","_jgr_browser_error","_jgr_browser_asset_count"]',
        '-sEXPORTED_RUNTIME_METHODS=["ccall","UTF8ToString"]', '-sASSERTIONS=1']
    link += ['-Wl,--wrap='+name for name in WRAPS]
    if args.debug: link += ['-g3']
    subprocess.run(link+['-o', str(js)], check=True)
    worker = '\n'.join(frontend(name) for name in ('filesystem.js', 'background.js'))+'\n'+js.read_text()+'\n'+frontend('worker.js')
    html = frontend('prepare.html')
    replacements = {
        '<!-- JGR_PREPARE_STYLE -->': '<style>'+frontend('style.css')+'</style>',
        '<!-- JGR_PREPARE_WORKER -->': '<script>globalThis.JGR_PREPARE_WORKER_SOURCE='+json.dumps(worker).replace('</', '<\\/')+';</script>',
        '<!-- JGR_PREPARE_SCRIPT -->': '<script>'+frontend('page.js').replace('</', '<\\/')+'</script>',
        notices.MARKER: notice_html}
    html_bytes = notices.replace_markers(html, replacements).encode('utf-8')
    release.bind_dependencies(compiled, psp)
    release.unchanged()
    report = dict(schema=1, status='built_not_yet_validated', compiler=version,
                  html_sha256=hashlib.sha256(html_bytes).hexdigest(), html_bytes=len(html_bytes),
                  converter_sha256=digest(js), recipe_and_assets_embedded=False,
                  source_release=release.metadata, source_base_url=args.source_base_url, notice_files=notice_files,
                  matching_source_compiler_dependencies=dependency_count,
                  frontend_files=[dict(path=str(path.relative_to(ROOT)), sha256=digest(path))
                                  for path in sorted(HERE.glob('*')) if path.is_file() and path.suffix in ('.js','.html','.css','.py','.txt','.json')],
                  expected_identity_header_sha256=digest(psp/'src/portable_asset_identity.h'),
                  source_files=[dict(path=str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path),
                                     sha256=digest(path)) for path in sorted({path for path,_ in sources} | {psp/'src'/name for name in closure if name != 'crash_build.inc'})],
                  link_command=link)
    # Prepare every output before publishing. The HTML is the final rename, so
    # a failed audit/template/license check leaves the previous page intact.
    with tempfile.TemporaryDirectory(prefix='.publish-', dir=output) as temporary:
        staged = Path(temporary)
        archive_name = release.metadata['archive_filename']
        (staged/archive_name).write_bytes(release.data)
        (staged/'prepare.html').write_bytes(html_bytes)
        (staged/'build.json').write_text(json.dumps(report, indent=2)+'\n')
        release.bind_dependencies(compiled, psp)
        release.unchanged()
        for name in (archive_name, 'build.json', 'prepare.html'):
            os.replace(staged/name, output/name)
    print(json.dumps({key:report[key] for key in ('status', 'compiler', 'html_sha256', 'html_bytes')}, indent=2))


if __name__ == '__main__': main()
