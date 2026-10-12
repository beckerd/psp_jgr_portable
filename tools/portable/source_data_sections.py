#!/usr/bin/env python3
"""Compiler-assisted migration of numeric generated arrays into runtime blobs.

The reviewed subset is deliberately narrow: top-level static const arrays
initialized exclusively with numeric literals. The target compiler supplies
the exact ABI, padding, float representation, dimensions and alignment.
Any unknown declaration or pointer relocation fails closed. No compiler ever
sees asset values while compiling the emitted public storage implementation.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

DECLARATION = re.compile(r"(?m)^[ \t]*(?:static\s+)?const\s+(?P<type>[A-Za-z_][\w \t]*?)\s+(?P<name>[A-Za-z_]\w*)\s*(?P<dims>(?:\[[^\]\n]*\])+)[ \t]*(?P<attr>__attribute__\(\(aligned\([0-9]+\)\)\))?\s*=\s*(?P<open>\{)")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
NUMBER = re.compile(r"[-+]?(?:0[xX][0-9a-fA-F]+(?:\.[0-9a-fA-F]*)?[pP][-+]?[0-9]+[fFlL]?|0[xX][0-9a-fA-F]+[uUlL]*|(?:[0-9]+\.[0-9]*|\.[0-9]+|[0-9]+)(?:[eE][-+]?[0-9]+)?[fFlLuU]*)")


def arrays(text: str) -> list[dict]:
    # Split comma-separated declarations while preserving each original
    # initializer. This supports e.g. a local start[]/cue[] pair without
    # confusing their inner aggregate commas for declaration separators.
    expanded = text
    while True:
        changed = False
        for match in DECLARATION.finditer(expanded):
            depth, at = 1, match.end()
            while at < len(expanded) and depth:
                depth += (expanded[at] == "{") - (expanded[at] == "}")
                at += 1
            continuation = re.match(r"\s*,\s*([A-Za-z_]\w*)\s*((?:\[[^]\n]*\])+)\s*=\s*\{", expanded[at:])
            if continuation:
                prefix = f";\nstatic const {match['type']} {continuation[1]}{continuation[2]}={{"
                expanded = expanded[:at] + prefix + expanded[at+continuation.end():]
                changed = True
                break
        if not changed: break
    text = expanded
    rows, previous = [], 0
    for match in DECLARATION.finditer(text):
        if match.start() < previous: continue
        depth, at = 1, match.end()
        while at < len(text) and depth:
            if text[at] == "{": depth += 1
            elif text[at] == "}": depth -= 1
            at += 1
        end = re.match(r"\s*;", text[at:])
        if depth or not end: raise ValueError("Unterminated generated array: " + match["name"])
        end = at + end.end()
        literal = COMMENT.sub("", text[match.end()-1:at])
        if re.sub(r"[\s{},()+*/%|&^~<>-]", "", NUMBER.sub("", literal)):
            raise ValueError("Non-numeric initializer requires another reviewed migration: " + match["name"])
        if re.sub(r"\s", "", COMMENT.sub("", text[previous:match.start()])):
            raise ValueError("Unknown declarations before " + match["name"])
        dims = re.findall(r"\[([^]]*)\]", match["dims"])
        if any(not re.fullmatch(r"[0-9]*", dim) for dim in dims) or any(not dim for dim in dims[1:]):
            raise ValueError("Only numeric dimensions or one inferred outer dimension are supported")
        rows.append({"name": match["name"], "type": match["type"].strip(), "dims": dims,
                     "prefix": text[match.start():match.end()-1],
                     "initializer": text[match.end()-1:at]})
        previous = end
    if not rows or re.sub(r"\s", "", COMMENT.sub("", text[previous:])):
        raise ValueError("Input is not exclusively reviewed numeric array declarations")
    if len({row["name"] for row in rows}) != len(rows): raise ValueError("Duplicate generated array names")
    return rows


def elf_arrays(image: bytes, rows: list[dict]) -> list[dict]:
    """Read bounded little-endian ELF32 sections/symbols; reject relocations."""
    if len(image) < 52 or image[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected the target compiler's little-endian ELF32 object")
    header = struct.unpack_from("<HHIIIIIHHHHHH", image, 16)
    if header[0] != 1 or header[1] != 8: raise ValueError("Expected a MIPS relocatable companion object")
    shoff, shsize, shnum, shnames = header[5], header[10], header[11], header[12]
    if shsize != 40 or not shnum or shnames >= shnum or shoff + shsize * shnum > len(image):
        raise ValueError("Invalid ELF section table")
    headers = [struct.unpack_from("<10I", image, shoff + i * shsize) for i in range(shnum)]

    def contents(section):
        offset, size = section[4], section[5]
        if offset > len(image) or size > len(image) - offset: raise ValueError("ELF section leaves object")
        return image[offset:offset+size]

    names = contents(headers[shnames])

    def string(table, offset):
        if offset >= len(table): raise ValueError("Invalid ELF string offset")
        end = table.find(b"\0", offset)
        if end < 0: raise ValueError("Unterminated ELF string")
        return table[offset:end].decode("ascii")

    section_names = [string(names, section[0]) for section in headers]
    symbols = {}
    for section in headers:
        if section[1] != 2: continue
        if section[6] >= shnum or section[9] != 16 or section[5] % 16: raise ValueError("Invalid ELF symbols")
        strings, values = contents(headers[section[6]]), contents(section)
        for offset in range(0, section[5], 16):
            name, value, size, info, _, index = struct.unpack_from("<IIIBBH", values, offset)
            if info & 15 == 1 and name: symbols[string(strings, name)] = (value, size, index)
    result = []
    for row in rows:
        section_name = ".jgr_asset_" + row["name"]
        try: index = section_names.index(section_name)
        except ValueError as exc: raise ValueError("Compiler omitted " + section_name) from exc
        for section in headers:
            if section[1] in (4, 9) and section[7] == index and section[5]:
                raise ValueError("Pointer/code relocation in numeric asset: " + row["name"])
        if row["name"] not in symbols: raise ValueError("Missing generated array symbol")
        offset, size, sym_index = symbols[row["name"]]
        data = contents(headers[index])
        if sym_index != index or not size or offset + size > len(data): raise ValueError("Invalid generated object extent")
        stride_index = section_names.index(".jgr_stride_" + row["name"])
        stride = struct.unpack_from("<I", contents(headers[stride_index]))[0]
        if not stride or size % stride: raise ValueError("Array dimension does not match compiler ABI")
        count = size // stride
        if row["dims"][0] and int(row["dims"][0]) != count: raise ValueError("Compiler changed an explicit array bound")
        alignment = max(16, headers[index][8])
        if alignment & (alignment-1) or alignment > 4096: raise ValueError("Unsupported asset alignment")
        result.append(row | {"count": count, "bytes": size, "alignment": alignment, "data": data[offset:offset+size]})
    return result


def compile_arrays(text, preamble, compiler, include_dirs):
    rows = arrays(text)
    private = [preamble]
    for row in rows:
        prefix = row["prefix"].rsplit("=", 1)[0]
        private.append(prefix + f' __attribute__((used,section(".jgr_asset_{row["name"]}")))=' + row["initializer"] + ";")
        private.append(f'const unsigned jgr_stride_{row["name"]} __attribute__((used,section(".jgr_stride_{row["name"]}")))=sizeof({row["name"]}[0]);')
    with tempfile.TemporaryDirectory(prefix="jgr-private-companion-") as temp:
        source, obj = Path(temp)/"companion.c", Path(temp)/"companion.o"
        source.write_text("\n".join(private) + "\n")
        command = [compiler, "-std=gnu99", "-O0", "-G0", "-c", str(source), "-o", str(obj)]
        for directory in include_dirs: command.extend(["-I", str(directory)])
        subprocess.run(command, check=True)
        return elf_arrays(obj.read_bytes(), rows)


def generate(text, preamble, compiler, include_dirs, name):
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name): raise ValueError("Unsafe asset group name")
    rows = compile_arrays(text, preamble, compiler, include_dirs)
    header_bytes = 32 + 16 * len(rows)
    blob = bytearray((header_bytes + 15) & ~15)
    abi = []
    for row in rows:
        blob.extend(bytes((-len(blob)) % row["alignment"]))
        offset = len(blob); blob.extend(row["data"])
        identity = zlib.crc32((name + "/" + row["name"]).encode())
        abi.append({"id": identity, "offset": offset, "bytes": row["bytes"], "alignment": row["alignment"]})
    directory = b"".join(struct.pack("<4I", row["id"], row["offset"], row["bytes"], row["alignment"]) for row in abi)
    blob[32:header_bytes] = directory
    layout_crc = zlib.crc32(directory)
    blob[:32] = struct.pack("<4s7I", b"JGSD", 1, len(rows), len(blob), layout_crc,
                           zlib.crc32(blob[header_bytes:]), header_bytes, 0)
    prefix = "source_data_" + name
    implementation = ["/* Compiler-derived BSS ABI; contains no original values. */", "#if JGR_PORTABLE_ASSETS", preamble,
                      '#include "source_data_sections.h"']
    guard = "JGR_" + prefix.upper() + "_INC"
    declarations = ["/* External mutable storage; generated from compiler ABI only. */",
                    "#ifndef " + guard, "#define " + guard]
    entries = []
    for row, record in zip(rows, abi):
        dims = f'[{row["count"]}]' + "".join("[" + dim + "]" for dim in row["dims"][1:])
        symbol = f'jgr_data_{name}_{row["name"]}'
        declaration = row["type"] + " " + row["name"] + dims + f' __asm__("{symbol}")'
        implementation.append(declaration + f' __attribute__((aligned({row["alignment"]})));')
        declarations.append("extern " + declaration + ";")
        declarations.append(f'typedef char {symbol}_abi_size[(sizeof({row["name"]})=={row["bytes"]}u)?1:-1];')
        entries.append("    {" + ",".join(map(str, (record["id"], record["offset"], record["bytes"], record["alignment"]))) + "," + row["name"] + "},")
    implementation += ["static const SourceDataSection sections[]={", *entries, "};",
        f'int {prefix}_load(void){{return source_data_sections_load("{prefix}.jsd",sections,{len(rows)}u,{len(blob)}u,0x{layout_crc:08x}u);}}', "#endif"]
    declarations.append("#endif")
    return {"blob": bytes(blob), "rows": rows, "layout": abi,
            "source": {prefix + ".c": ("\n".join(implementation) + "\n").encode(),
                       prefix + ".inc": ("\n".join(declarations) + "\n").encode(),
                       prefix + ".h": (f'int {prefix}_load(void);\n').encode()},
            "manifest": {"kind": "compiler_numeric_sections_v1", "name": name,
                         "source_sha256": hashlib.sha256(text.encode()).hexdigest(),
                         "preamble_sha256": hashlib.sha256(preamble.encode()).hexdigest(),
                         "blob_sha256": hashlib.sha256(blob).hexdigest(), "bytes": len(blob),
                         "sections": len(rows), "relocations": 0,
                         "upstream_disc_provenance": "must be verified by calling extraction pipeline"}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--preamble", type=Path, required=True, help="Reviewed include/type declarations, no asset data")
    parser.add_argument("--cc", required=True)
    parser.add_argument("--include-dir", type=Path, action="append", default=[])
    parser.add_argument("--name", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.out.exists() or args.out.is_symlink(): raise ValueError("Output must be new")
        result = generate(args.input.read_text(), args.preamble.read_text(), args.cc, args.include_dir, args.name)
        args.out.mkdir(parents=True)
        source = args.out / "source/psp/src"; source.mkdir(parents=True)
        private = args.out / "PRIVATE-assets"; private.mkdir()
        for name, data in result["source"].items(): (source / name).write_bytes(data)
        (private / ("source_data_" + args.name + ".jsd")).write_bytes(result["blob"])
        (args.out / "receipt.json").write_text(json.dumps(result["manifest"] | {"layout": result["layout"]}, indent=2) + "\n")
        print(json.dumps(result["manifest"], indent=2))
    except (ValueError, OSError, subprocess.CalledProcessError, struct.error) as exc:
        parser.exit(1, f"source-data-sections: {exc}\n")


if __name__ == "__main__":
    main()
