"""Select converter dependencies without exporting developer oracle fixtures."""
import ast
from pathlib import Path
import re
import tempfile


NAME = re.compile(r'\b([A-Za-z_][A-Za-z_0-9]*\.(?:py|lua|mjs))\b')


def references(text):
    result=set(NAME.findall(text))
    try:parsed=ast.parse(text)
    except SyntaxError:return result
    for node in ast.walk(parsed):
        if isinstance(node,ast.Import):result.update(n.name.split('.')[0]+'.py' for n in node.names)
        elif isinstance(node,ast.ImportFrom) and node.module:result.add(node.module.split('.')[0]+'.py')
    return result


def select(root):
    from pipeline import graph
    from capture import SCRIPTS
    root=Path(root)
    source=root/'decompile/tools'
    pending={'package_psp.py','crash_build_id.py'}
    pending.update(pair[0] for pair in SCRIPTS.values())
    # Construct every mission node from filenames alone. No game payload is
    # needed to discover the conversion DAG's native source dependencies.
    with tempfile.TemporaryDirectory(prefix='jgr-converter-graph-') as temp:
        tree=Path(temp)/'tree'
        banks=tree/'decompile/afs/MISSIONSET';banks.mkdir(parents=True)
        for n in range(64):(banks/('%03d_bank.bin'%n)).touch()
        for node in graph(tree):
            for command in node.commands:
                for argument in command:pending.update(NAME.findall(argument))
    for path in (root/'tools/portable').glob('*.py'):
        if path.stem.startswith('test_') or path.stem.endswith('_test'):
            continue
        pending.update(references(path.read_text()))
    found=set()
    while pending:
        name=pending.pop()
        path=source/name
        if name in found or not path.is_file():continue
        found.add(name)
        pending.update(references(path.read_text())-found)
    return sorted(found)


def exported_lua(text):
    # Public capture scripts get their root from the isolated runner.
    return re.sub(r"local root\s*=\s*['\"]/Users/[^\n]*?/JGR/decompile/['\"]",
        "local root=assert(os.getenv('JGR_REFERENCE_ROOT'),'Use tools/portable/capture.py')",text)
