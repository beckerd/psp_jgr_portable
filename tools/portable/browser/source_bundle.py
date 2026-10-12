"""Deterministic, audited matching source for a browser preparer release.

Only source_check's reviewed inventory enters the archive. Build outputs,
private game inputs and Git history are never selected by a recursive copy.
"""
import hashlib
import io
import json
from pathlib import Path
import shlex
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import source_check

GUIDES = ('README.md', 'tools/portable/README.md', 'tools/portable/browser/README.md')
LICENSE_FILES = ('LICENSE', 'THIRD_PARTY_NOTICES.md')
RECEIPTS = ('EXPORT-REVIEW.json', 'PUBLIC-CONTENT-REVIEW.json')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode('utf-8')


def content_fingerprint(files):
    """Source-content cache binding, independent of restored file mtimes."""
    return sha(json_bytes({name: sha(data) for name, data in sorted(files.items())}))


def dependency_paths(obj):
    words = shlex.split(obj.with_suffix('.d').read_text().replace('\\\n', ' '))
    if len(words) < 2 or words[0] != str(obj) + ':' or any(not Path(word).is_absolute() for word in words[1:]):
        raise ValueError('Invalid compiler dependency receipt: ' + obj.name)
    return [Path(word) for word in words[1:]]


def archive(files, receipts, tree_hash, compiler, debug=False):
    """No host paths, timestamps or compression-library variation in the ZIP."""
    for name in GUIDES:
        if name not in files:
            raise ValueError('Missing source build guide: ' + name)
    for name in LICENSE_FILES:
        if name not in files or not files[name].strip():
            raise ValueError('Missing or empty source license notice: ' + name)
    for name in files:
        source_check.public_review.safe_path(name)
        if name.split('/')[0] in ('.git', 'build'):
            raise ValueError('Non-source archive input: ' + name)
    manifest = dict(schema=1, source_tree_sha256=tree_hash,
        source_directory='jgr-portable-source', contains_git_history=False,
        build_guides=list(GUIDES), license_files=list(LICENSE_FILES),
        browser_toolchain=dict(emscripten=compiler, python='3.10+', git='required',
                              debug=bool(debug), command='python3 tools/portable/browser/build.py --emcc /path/to/emcc' +
                              (' --debug' if debug else '')),
        files=[dict(path=name, bytes=len(data), sha256=sha(data)) for name, data in sorted(files.items())])
    members = {'jgr-portable-source/' + name: data for name, data in files.items()}
    members.update({'source-review/' + name: receipts[name] for name in RECEIPTS})
    members['SOURCE-RELEASE.json'] = json_bytes(manifest)
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_STORED) as bundle:
        for name, data in sorted(members.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            bundle.writestr(info, data)
    data = output.getvalue()
    digest = sha(data)
    return data, dict(archive_filename='jgr-portable-source-' + digest + '.zip',
        archive_sha256=digest, archive_bytes=len(data), source_tree_sha256=tree_hash,
        source_files=len(files), manifest_sha256=sha(members['SOURCE-RELEASE.json']))


class SourceRelease:
    def __init__(self, root, identity, compiler, debug=False):
        self.root = Path(root).resolve()
        self.identity = Path(identity).resolve()
        with tempfile.TemporaryDirectory(prefix='jgr-browser-source-') as temporary:
            snapshot = Path(temporary).resolve() / 'source'
            report = source_check.check(self.root, snapshot, self.identity)
            receipts = {name: (snapshot / name).read_bytes() for name in RECEIPTS}
            rows = json.loads(receipts['EXPORT-REVIEW.json'])['files']
            self.files = {row['path']: (snapshot / row['path']).read_bytes()
                          for row in rows if row['path'] not in RECEIPTS}
        self.data, self.metadata = archive(self.files, receipts, report['source_tree_sha256'], compiler, debug)

    def bind(self, paths, psp):
        """Bind real project compiler inputs, including an external --psp tree."""
        psp = Path(psp).resolve()
        files = {}
        for original in paths:
            path = Path(original).absolute()
            if path.is_symlink() or any(parent.is_symlink() for parent in path.parents):
                raise ValueError('Linked compiler input: ' + str(path))
            path = path.resolve()
            if path.is_relative_to(psp):
                name = 'psp/' + path.relative_to(psp).as_posix()
            elif path.is_relative_to(self.root):
                name = path.relative_to(self.root).as_posix()
            else:
                raise ValueError('Compiler input outside matching source: ' + str(path))
            if name not in self.files or path.read_bytes() != self.files[name]:
                raise ValueError('Compiler input differs from matching source: ' + name)
            files[name] = self.files[name]
        return files

    def bind_dependencies(self, objects, psp):
        inputs = set()
        for obj in objects:
            inputs.update(dependency_paths(obj))
        self.bind(inputs, psp)
        return len(inputs)

    def unchanged(self):
        # Repeat both identity validation and the no-unreviewed-files check.
        if source_check.capture(self.root, self.identity) != self.files:
            raise ValueError('Reviewed source changed during browser build')
