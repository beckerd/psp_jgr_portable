"""Exact license inputs and safe, visible HTML for the offline preparer."""
import hashlib
import html
import re

NOTICE_PATHS = (
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
    'tools/portable/browser/THIRD_PARTY_NOTICES.txt',
    'tools/portable/notices/Pillow-11.3.0.txt',
    'tools/portable/notices/FreeBSD-sort.txt',
    'tools/portable/notices/Flycast-hardware.txt',
    'tools/portable/notices/Flycast-GPL-2.0.txt',
)
MARKER = '<!-- JGR_PREPARE_NOTICES -->'


def collect(files):
    """Use captured source bytes, retaining every license text verbatim."""
    sections, rows = [], []
    for name in NOTICE_PATHS:
        if name not in files:
            raise ValueError('Missing required license notice: ' + name)
        data = files[name]
        text = data.decode('utf-8')
        if not text.strip() or '\0' in text:
            raise ValueError('Empty or invalid license notice: ' + name)
        sections.append(name + '\n' + '=' * len(name) + '\n' + text)
        rows.append(dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    return '\n\n'.join(sections), rows


def render(text, release):
    digest = release['archive_sha256']
    if not re.fullmatch('[0-9a-f]{64}', digest):
        raise ValueError('Invalid source archive checksum')
    filename = 'jgr-portable-source-' + digest + '.zip'
    if release['archive_filename'] != filename:
        raise ValueError('Source archive filename must contain its exact checksum')
    return ('<p class="source-notice">Matching source: <a href="' + filename + '" download>' +
            filename + '</a><br>SHA-256: <span class="source-checksum">' + digest +
            '</span></p>\n<pre id="third-party-notices" tabindex="0" '
            'aria-label="Project and third-party license notices">' + html.escape(text, quote=True) + '</pre>')


def replace_markers(template, replacements):
    # Validate every marker before making any replacement or writing outputs.
    for marker in replacements:
        if template.count(marker) != 1:
            raise ValueError('Template marker missing or repeated: ' + marker)
    for marker, value in replacements.items():
        template = template.replace(marker, value)
    return template
