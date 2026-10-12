#!/usr/bin/env python3
"""Report public-source findings separately from the known-symbol BSS audit.

This is a conservative engineering gate, not an authorship/license certificate.
Text-only files can contain original assets. A changed hash never closes a
finding: its recorded disposition must be reviewed explicitly.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

POLICY_PATH = 'tools/portable/public_review.json'
RECEIPTS = {'EXPORT-REVIEW.json', 'PUBLIC-CONTENT-REVIEW.json'}
POLICY_FILES = {POLICY_PATH, 'tools/portable/public_sources.json'}
EXCLUDED_METADATA = RECEIPTS | POLICY_FILES
FORBIDDEN_SUFFIXES = {'.bin', '.gdi', '.cdi', '.chd', '.afs', '.adx', '.png',
                      '.jpg', '.jpeg', '.wav', '.mp3', '.raw', '.pbp', '.elf',
                      '.prx', '.o', '.a', '.so', '.jsd', '.jgc', '.jgs', '.sav'}
# The creator explicitly authorized these two project-authored XMB images.
# This is an exact identity exception, not permission for game-disc artwork,
# other PNGs, or replacements. Normal reviewed_files binding still applies.
PROJECT_ARTWORK = {
    'icon/ICON0.png': '6958841a6c9ee93bb36d84332456c1bd774d858a5148bd1a324a9846dfb5ac31',
    'icon/PIC1.png': 'a0989ceaf1a38e702870583e4a4642289707e806041efdffc6ce78d1190caec3',
}
# Model-spec texture bindings must read the mapping from the supplied disc.
# A literal map is decoded asset data even when it has only one texture slot.
# Limit this classification to public spec files: converter code and synthetic
# unit tests legitimately describe or construct the legacy literal syntax.
_TEXMAP_NUMBER = r'[+-]?(?:0[xX][0-9a-fA-F]+|[0-9]+)'
_LITERAL_TEXMAP = re.compile(
    r'(?:^|:)[ \t]*texmap[ \t]*=[ \t]*' + _TEXMAP_NUMBER +
    r'(?:[ \t]*,[ \t]*' + _TEXMAP_NUMBER + r')*[ \t]*(?=:|#|$)', re.IGNORECASE)


def decoded_texmap_lines(name, text):
    """Find decoded mappings independently of the reviewed-file hash ledger."""
    if not PurePosixPath(name).name.lower().endswith('_spec.txt'):
        return []
    return [line for line, content in enumerate(text.splitlines(), 1)
            if _LITERAL_TEXMAP.search(content)]


def safe_path(name):
    if not isinstance(name, str):
        raise ValueError('Review paths must be strings')
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '\\' in name or any(p in ('', '.', '..') for p in name.split('/')):
        raise ValueError('Unsafe review path: ' + name)
    return path


def valid_hash(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def file_rows(rows):
    """Validate the exact, data-free inventory schema shared with receipts."""
    if not isinstance(rows, list):
        raise ValueError('Review inventory must be a list')
    result = {}
    for row in rows:
        if not isinstance(row, dict) or set(row) != {'path', 'sha256', 'bytes'}:
            raise ValueError('Invalid review inventory row')
        name = str(safe_path(row['path']))
        if name in result or not valid_hash(row['sha256']) or type(row['bytes']) is not int or row['bytes'] < 0:
            raise ValueError('Invalid/duplicate review inventory entry: ' + name)
        result[name] = row
    return result


def scan(root):
    root = Path(root)
    if not root.is_dir() or root.is_symlink():
        raise ValueError('Review requires an existing source directory, not a link')
    files, failures, metadata = [], [], {}
    for path in sorted(root.rglob('*')):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            failures.append('Symlink in source export: ' + name)
            continue
        if '.git' in path.relative_to(root).parts:
            failures.append('Git history in source export: ' + name)
        if not path.is_file():
            continue
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        files.append({'path': name, 'sha256': digest, 'bytes': len(data)})
        project_artwork = PROJECT_ARTWORK.get(name) == digest
        if name in PROJECT_ARTWORK and not project_artwork:
            failures.append('Project artwork identity changed: ' + name)
        if path.suffix.lower() in FORBIDDEN_SUFFIXES and not project_artwork:
            failures.append('Asset/build output in source export: ' + name)
        if not project_artwork:
            try:
                text = data.decode('utf-8')
                failures.extend('Decoded literal texmap mapping in public spec: ' + name + ':' + str(line)
                                for line in decoded_texmap_lines(name, text))
                if b'\0' in data:
                    failures.append('Binary bytes in source export: ' + name)
            except UnicodeDecodeError:
                failures.append('Non-text source export file: ' + name)
        if name in EXCLUDED_METADATA:
            try:
                metadata[name] = json.loads(data)
            except (ValueError, UnicodeDecodeError):
                failures.append('Invalid JSON review metadata: ' + name)
    if not any(row['path'] not in EXCLUDED_METADATA for row in files):
        failures.append('Empty source export')
    return files, failures, metadata


def inventory(root):
    """Candidate rows for human review; generating them does not approve them.

    Four self-referential policy/receipt files are excluded. Their syntax and
    bindings are checked separately; their prose is outside the asset review.
    """
    files, failures, _ = scan(root)
    if failures:
        raise ValueError('\n'.join(failures))
    return [row for row in files if row['path'] not in EXCLUDED_METADATA]


def review(root, policy=None, *, strict_code=False):
    root = Path(root)
    all_files, failures, metadata = scan(root)
    if policy is None:
        if POLICY_PATH not in metadata:
            raise ValueError('Missing target-tree public review policy: ' + POLICY_PATH)
        policy = metadata[POLICY_PATH]
    if (not isinstance(policy, dict) or type(policy.get('schema')) is not int or policy['schema'] != 1
            or not isinstance(policy.get('findings'), list)
            or type(policy.get('whole_tree_review_complete')) is not bool
            or set(policy) - {'schema', 'findings', 'whole_tree_review_complete', 'reviewed_files', 'review_note'}):
        raise ValueError('Invalid public-content review policy')
    if 'review_note' in policy and not isinstance(policy['review_note'], str):
        raise ValueError('Invalid public review note')
    if POLICY_PATH in metadata and metadata[POLICY_PATH] != policy:
        failures.append('Applied public review policy differs from exported policy')
    if 'tools/portable/public_sources.json' in metadata:
        from export_inputs import validate
        try:
            validate(metadata['tools/portable/public_sources.json'])
        except ValueError as exc:
            failures.append(str(exc))
    files = [row for row in all_files if row['path'] not in EXCLUDED_METADATA]
    expected = file_rows(policy.get('reviewed_files', []))
    if expected.keys() & EXCLUDED_METADATA:
        raise ValueError('Self-referential review metadata must not appear in reviewed_files')
    actual = {row['path']: row for row in files}
    inventory_failures = ['Unreviewed export file: ' + name for name in sorted(actual.keys() - expected.keys())]
    inventory_failures += ['Reviewed export file missing: ' + name for name in sorted(expected.keys() - actual.keys())]
    inventory_failures += ['Reviewed export file changed: ' + name for name in sorted(actual.keys() & expected.keys()) if actual[name] != expected[name]]
    failures += inventory_failures
    hashes = {row['path']: row['sha256'] for row in files}
    findings = []
    ids = set()
    for row in policy['findings']:
        if not isinstance(row, dict) or set(row) - {'id', 'category', 'path', 'line', 'evidence', 'remediation', 'status', 'resolution'}:
            raise ValueError('Invalid finding schema')
        if not isinstance(row.get('id'), str) or not row['id'].strip() or not isinstance(row.get('evidence'), str) or not row['evidence'].strip():
            raise ValueError('Finding lacks ID/evidence')
        if 'path' not in row:
            raise ValueError('Finding lacks path: ' + row['id'])
        safe_path(row['path'])
        if row['path'] in EXCLUDED_METADATA:
            raise ValueError('Finding references excluded review metadata: ' + row['id'])
        if ('line' in row and (type(row['line']) is not int or row['line'] < 1)) or ('remediation' in row and not isinstance(row['remediation'], str)):
            raise ValueError('Invalid finding metadata: ' + row['id'])
        if row['id'] in ids or row.get('category') not in ('original_asset_data', 'translated_game_code', 'review_candidate'):
            raise ValueError('Invalid or duplicate finding: ' + row['id'])
        ids.add(row['id'])
        status = row.get('status', 'open')
        if status not in ('open', 'resolved'):
            raise ValueError('Invalid finding disposition: ' + row['id'])
        item = dict(row, present=row['path'] in hashes, current_sha256=hashes.get(row['path']))
        if status == 'resolved':
            # A resolution is bound to the replacement, or to file removal.
            resolution = row.get('resolution', {})
            if (not isinstance(resolution, dict) or set(resolution) != {'evidence', 'sha256', 'disposition'}
                    or not isinstance(resolution['evidence'], str) or not resolution['evidence'].strip()
                    or resolution['disposition'] not in ('removed', 'replaced', 'retained_code')):
                raise ValueError('Resolution lacks evidence/identity: ' + row['id'])
            removed = resolution['disposition'] == 'removed'
            if (removed and resolution['sha256'] is not None) or (not removed and not valid_hash(resolution['sha256'])):
                raise ValueError('Resolution identity does not match disposition: ' + row['id'])
            if resolution['disposition'] == 'retained_code' and row['category'] != 'translated_game_code':
                raise ValueError('Only translated code can be retained by code policy: ' + row['id'])
            if resolution['sha256'] != item['current_sha256']:
                item['status'] = 'open'
                item['resolution_stale'] = True
        else:
            item['status'] = 'open'
        findings.append(item)
    asset_blockers = [row['id'] for row in findings if row['status'] == 'open' and
                      row['category'] != 'translated_game_code']
    code_blockers = [row['id'] for row in findings if row['category'] == 'translated_game_code' and
                     (row['status'] == 'open' or row.get('resolution', {}).get('disposition') == 'retained_code')]
    blockers = list(asset_blockers)
    if strict_code:
        blockers += code_blockers
    if policy.get('whole_tree_review_complete') is not True:
        blockers.append('whole-export-content-review-incomplete')
    binding = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    report = {'schema': 1, 'passed': not failures and not blockers,
            'policy': 'reject-known-translated-programs' if strict_code else 'asset-review-with-disclosed-reconstructed-code',
            'scope': 'Human-reviewed source inventory and known findings only; policy/receipt metadata prose is excluded. No legal, independent-authorship or absence-of-all-derived-code certification.',
            'excluded_metadata': sorted(EXCLUDED_METADATA),
            'whole_tree_review_complete': policy['whole_tree_review_complete'] and not inventory_failures and bool(files),
            'original_asset_absence_verified': not failures and not asset_blockers and policy.get('whole_tree_review_complete') is True,
            'known_translated_code_present': any(r['present'] and r['id'] in code_blockers for r in findings),
            'files': len(files), 'bytes': sum(r['bytes'] for r in files), 'source_tree_sha256': binding,
            'policy_sha256': hashlib.sha256(json.dumps(policy, sort_keys=True).encode()).hexdigest(),
            'inventory_failures': failures, 'blockers': blockers, 'findings': findings}
    receipt = metadata.get('PUBLIC-CONTENT-REVIEW.json')
    if 'PUBLIC-CONTENT-REVIEW.json' in metadata:
        expected_report = dict(report)
        # Either supported mode may have written the receipt. Its full content
        # must equal a current result, not merely claim a passing Boolean.
        receipt_strict = isinstance(receipt, dict) and receipt.get('policy') == 'reject-known-translated-programs'
        receipt_blockers = list(asset_blockers) + (code_blockers if receipt_strict else [])
        if not policy['whole_tree_review_complete']:
            receipt_blockers.append('whole-export-content-review-incomplete')
        expected_report.update(policy='reject-known-translated-programs' if receipt_strict else 'asset-review-with-disclosed-reconstructed-code',
                               blockers=receipt_blockers, passed=not failures and not receipt_blockers)
        if receipt != expected_report:
            failures.append('Stale or invalid PUBLIC-CONTENT-REVIEW.json')
    receipt = metadata.get('EXPORT-REVIEW.json')
    if 'EXPORT-REVIEW.json' in metadata:
        try:
            validate_export_receipt(receipt, all_files, metadata.get('PUBLIC-CONTENT-REVIEW.json'))
        except ValueError as exc:
            failures.append(str(exc))
    report['passed'] = not failures and not blockers
    report['original_asset_absence_verified'] = not failures and not asset_blockers and report['whole_tree_review_complete']
    return report


def validate_export_receipt(receipt, all_files, content):
    """Receipts are generated metadata, never an unchecked text hiding place."""
    flags = {'contains_git_history', 'distribution_ready', 'runtime_source_provenance_ready',
             'source_only_build_ready', 'compiled_data_separation_verified', 'public_content_review_passed'}
    counts = {'original_input_inventory_count', 'portable_compile_original_input_count', 'reviewed_tool_build_and_metadata_inputs'}
    keys = flags | counts | {'schema', 'known_public_content_blockers', 'build_audit', 'remaining_gates', 'files'}
    if not isinstance(receipt, dict) or set(receipt) != keys or type(receipt['schema']) is not int or receipt['schema'] != 1:
        raise ValueError('Invalid EXPORT-REVIEW.json schema')
    if any(type(receipt[k]) is not bool for k in flags) or any(type(receipt[k]) is not int or receipt[k] < 0 for k in counts):
        raise ValueError('Invalid EXPORT-REVIEW.json field types')
    if receipt['contains_git_history'] or receipt['distribution_ready']:
        raise ValueError('EXPORT-REVIEW.json makes unsupported release claims')
    if not isinstance(content, dict) or receipt['public_content_review_passed'] != content.get('passed') or receipt['known_public_content_blockers'] != content.get('blockers'):
        raise ValueError('EXPORT-REVIEW.json does not match content review')
    expected = {row['path']: row for row in all_files if row['path'] != 'EXPORT-REVIEW.json'}
    if file_rows(receipt['files']) != expected:
        raise ValueError('EXPORT-REVIEW.json file inventory is stale')
    gates = ['complete disc conversion', 'third-party and code license choice/review', 'PSP hardware validation']
    if not receipt['public_content_review_passed']:
        gates.insert(0, 'public source/content review')
    if receipt['build_audit'] is None:
        gates.insert(int(not receipt['public_content_review_passed']), 'compiled embedded-data separation audit')
    else:
        audit = receipt['build_audit']
        from export import BUILD_AUDIT_SCOPE
        audit_counts = {'external_asset_symbols', 'external_asset_bytes', 'compiler_dependency_files'}
        audit_hashes = {'elf_sha256', 'eboot_sha256', 'asset_identity_sha256', 'audit_sha256'}
        split_keys = {'split_executable'} if isinstance(audit, dict) and 'split_executable' in audit else set()
        if (not isinstance(audit, dict) or set(audit) != audit_counts | audit_hashes | {'source_binding', 'active_original_data_includes', 'scope'} | split_keys
                or any(type(audit[k]) is not int or audit[k] < 1 for k in audit_counts)
                or any(not valid_hash(audit[k]) for k in audit_hashes)
                or audit['active_original_data_includes'] != [] or audit['scope'] != BUILD_AUDIT_SCOPE):
            raise ValueError('Invalid EXPORT-REVIEW.json build audit metadata')
        if split_keys:
            from audit_build import validate_split_metadata
            split = validate_split_metadata(audit['split_executable'])
            if audit['compiler_dependency_files'] != split['setup_dependency_files'] + split['game_dependency_files']:
                raise ValueError('Invalid split dependency counts')
        binding = audit['source_binding']
        if (not isinstance(binding, dict) or set(binding) != {'schema', 'sha256', 'files'}
                or type(binding['schema']) is not int or binding['schema'] != 1 or not valid_hash(binding['sha256'])
                or type(binding['files']) is not int or binding['files'] < 1):
            raise ValueError('Invalid EXPORT-REVIEW.json build source binding')
    if receipt['remaining_gates'] != gates or receipt['compiled_data_separation_verified'] != (receipt['build_audit'] is not None):
        raise ValueError('Invalid EXPORT-REVIEW.json gate metadata')


def require_clean(report):
    if report.get('passed') is not True:
        raise ValueError('Public export refused: ' + ', '.join(report['blockers'] + report['inventory_failures']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--strict-code', action='store_true')
    args = parser.parse_args()
    try:
        report = review(args.tree, strict_code=args.strict_code)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'findings'}, indent=2))
        require_clean(report)
    except (OSError, ValueError) as exc:
        parser.exit(1, str(exc) + '\n')


if __name__ == '__main__':
    main()
