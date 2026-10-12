#!/usr/bin/env python3
"""Prototype an asset-free compiled spray-bubble renderer with runtime BSS data.

Consumes a locally generated spray_bubble_mesh.inc, never a downloaded asset.
Outputs separate source/ and PRIVATE-assets/ trees. Only source/ is public-code
material; the private blob must be installed by the user's local asset setup.
The blob is private generated game content. The source output contains only
typed storage, layout metadata and validation/loading implementation. Upstream
disc provenance must be established by the calling extraction pipeline.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
import zlib

HEADER = struct.Struct("<4s7I")
ENTRY = struct.Struct("<4I")
VERSION = 1
MAGIC = b"JGBL"
MAX_SOURCE_BYTES = 1024 * 1024


def _body(text: str, name: str, typename: str) -> str:
    pattern = (r"static const " + re.escape(typename) + r"\s+" + re.escape(name) +
               r"\[\]\s*(?:__attribute__\(\(aligned\(16\)\)\))?\s*=\s*\{(.*?)\};")
    rows = re.findall(pattern, text, re.S)
    if len(rows) != 1:
        raise ValueError(f"Expected exactly one generated declaration: {name}")
    return rows[0]


def _integer(value: str) -> int:
    if not re.fullmatch(r"(?:0x[0-9a-fA-F]+|[0-9]+)[uU]?", value):
        raise ValueError(f"Not a generated integer literal: {value!r}")
    value = value.rstrip("uU")
    return int(value, 16 if value.startswith("0x") else 10)


def _float(value: str) -> float:
    if not re.fullmatch(r"-?0x[0-9a-fA-F]+\.[0-9a-fA-F]+p[+-][0-9]+f", value):
        raise ValueError("Expected generated hexadecimal binary32 literal")
    return float.fromhex(value[:-1])


def parse_mesh(text: str) -> list[dict]:
    """Decode the narrow generated C grammar without eval or a C compiler."""
    if len(text.encode()) > MAX_SOURCE_BYTES:
        raise ValueError("Spray-bubble source exceeds the bounded prototype limit")
    sections = []
    for i in range(3):
        vertices = _body(text, f"bubble_{i}", "BubbleVertex")
        rows = re.findall(r"\{([^{}]+)\}", vertices)
        if re.sub(r"\{[^{}]+\}|[\s,]", "", vertices) or not rows:
            raise ValueError("Malformed generated vertex rows")
        data = bytearray()
        for row in rows:
            fields = [f.strip() for f in row.split(",")]
            if len(fields) != 6:
                raise ValueError("Bubble vertex must contain six fields")
            data.extend(struct.pack("<ffIfff", _float(fields[0]), _float(fields[1]),
                                    _integer(fields[2]), *map(_float, fields[3:])))
        sections.append({"name": f"jgr_bubble_v{i}", "ctype": "BubbleVertex", "kind": 1,
                         "count": len(rows), "data": bytes(data), "width": 24})
        indices = _body(text, f"bubble_idx_{i}", "unsigned short")
        values = [_integer(value.strip()) for value in indices.split(",") if value.strip()]
        if not values or max(values) >= 65536:
            raise ValueError("Invalid bubble index storage")
        sections.append({"name": f"jgr_bubble_i{i}", "ctype": "unsigned short", "kind": 2,
                         "count": len(values), "data": struct.pack("<"+"H"*len(values), *values), "width": 2})
        batches = _body(text, f"bubble_batch_{i}", "BubbleBatch")
        rows = re.findall(r"\{([^{}]+)\}", batches)
        if re.sub(r"\{[^{}]+\}|[\s,]", "", batches) or len(rows) != 2:
            raise ValueError("Renderer requires two generated bubble batches")
        data = bytearray()
        vertex_count = sections[-2]["count"]
        for row in rows:
            fields = [_integer(f.strip()) for f in row.split(",")]
            if len(fields) != 5:
                raise ValueError("Bubble batch must contain five fields")
            vertex, first, count, texture, flags = fields
            if not count or first+count > len(values) or vertex+max(values[first:first+count]) >= vertex_count:
                raise ValueError("Bubble batch references outside its geometry")
            data.extend(struct.pack("<5I", *fields))
        sections.append({"name": f"jgr_bubble_b{i}", "ctype": "BubbleBatch", "kind": 3,
                         "count": len(rows), "data": bytes(data), "width": 20})
    return sections


def make_blob(sections: list[dict]) -> tuple[bytes, list[dict], int]:
    header_bytes = HEADER.size+len(sections)*ENTRY.size
    offset = (header_bytes+15)&~15
    payload = bytearray(offset-header_bytes)
    entries, layout = [], []
    for section in sections:
        padding = (-offset)&15
        payload.extend(bytes(padding));offset += padding
        entry = (section["kind"], section["count"], offset, len(section["data"]))
        entries.append(ENTRY.pack(*entry))
        layout.append({k: section[k] for k in ("name", "ctype", "kind", "count", "width")} |
                      {"offset": offset, "bytes": len(section["data"])})
        payload.extend(section["data"]);offset += len(section["data"])
    directory = b"".join(entries)
    layout_crc = zlib.crc32(directory)
    header = HEADER.pack(MAGIC, VERSION, len(sections), offset, layout_crc,
                         zlib.crc32(payload), header_bytes, 0)
    return header+directory+payload, layout, layout_crc


LOADER = r'''/* Generated ABI/storage only: no original mesh, index or material bytes. */
#include "source_data_bubble.h"
#include "source_data_io.h"
#if JGR_PORTABLE_ASSETS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef __PSP__
#include <pspkernel.h>
#endif
typedef struct {float u,v;unsigned colour;float x,y,z;} BubbleVertex;
typedef struct {unsigned vertex,index,count,texture,flags;} BubbleBatch;
typedef char vertex_layout_must_be_24[sizeof(BubbleVertex)==24 ? 1:-1];
typedef char batch_layout_must_be_20[sizeof(BubbleBatch)==20 ? 1:-1];
typedef char ushort_layout_must_be_2[sizeof(unsigned short)==2 ? 1:-1];
@STORAGE@
typedef struct {unsigned kind,count,offset,bytes;void *destination;} BlobSection;
static const BlobSection sections[]={
@SECTIONS@
};
static int ready;
static unsigned word(const unsigned char *p)
{return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static unsigned crc32(const unsigned char *p,size_t bytes)
{
    return source_data_io_checksum(p,bytes);
}
int source_data_bubble_ready(void){return ready;}
int source_data_bubble_load(const void *image,size_t bytes)
{
    const unsigned char *p=image;const unsigned endian=1;
    if(!image || bytes!=@BYTES@u || *(const unsigned char *)&endian!=1)return -1;
    if(memcmp(p,"JGBL",4) || word(p+4)!=1 || word(p+8)!=9 || word(p+12)!=bytes ||
       word(p+16)!=@LAYOUT_CRC@u || word(p+24)!=176 || word(p+28))return -2;
    if(crc32(p+32,144)!=@LAYOUT_CRC@u || crc32(p+176,bytes-176)!=word(p+20))return -3;
    for(unsigned i=0;i<9;i++) {
        const BlobSection *s=sections+i;const unsigned char *entry=p+32+16*i;
        if(word(entry)!=s->kind || word(entry+4)!=s->count || word(entry+8)!=s->offset ||
           word(entry+12)!=s->bytes || s->offset>bytes || s->bytes>bytes-s->offset)return -4;
    }
    /* A checksummed but malformed locally generated mesh must still never
       let the renderer index outside these fixed storage objects. */
    for(unsigned mesh=0;mesh<3;mesh++) {
        const BlobSection *v=sections+mesh*3,*indices=v+1,*b=v+2;
        for(unsigned n=0;n<v->count;n++)for(unsigned field=0;field<6;field++) {
            if(field==2)continue; /* Packed colour is not a float. */
            if((word(p+v->offset+n*24+field*4)&0x7f800000u)==0x7f800000u)return -8;
        }
        for(unsigned n=0;n<b->count;n++) {
            const unsigned char *batch=p+b->offset+n*20;
            unsigned base=word(batch),first=word(batch+4),count=word(batch+8);
            if(base>=v->count || first>=indices->count || !count || count>indices->count-first)return -8;
            for(unsigned k=0;k<count;k++) {
                const unsigned char *index=p+indices->offset+(first+k)*2;
                unsigned vertex=index[0]|((unsigned)index[1]<<8);
                if(vertex>=v->count-base)return -8;
            }
        }
    }
    /* Validation completes before the first write: failed replacement retains
       the previous fully loaded asset. Called only before drawing begins. */
    for(unsigned i=0;i<9;i++)memcpy(sections[i].destination,p+sections[i].offset,sections[i].bytes);
#ifdef __PSP__
    /* Publish once during setup before any GE/ME observer can use the data. */
    for(unsigned i=0;i<9;i++)sceKernelDcacheWritebackRange(sections[i].destination,sections[i].bytes);
#endif
    ready=1;return 0;
}
int source_data_bubble_load_file(const char *path)
{
    const void *memory=NULL;size_t memory_bytes=0;
    int view=source_data_io_view(path,&memory,&memory_bytes);
    if(view)return view<0 ? -5:source_data_bubble_load(memory,memory_bytes);
    FILE *f=source_data_io_open(path);if(!f)return -5;
    unsigned char *data=malloc(@BYTES@u);if(!data){fclose(f);return -6;}
    size_t n=fread(data,1,@BYTES@u,f);
    int failed=n!=@BYTES@u || fgetc(f)!=EOF || ferror(f);
    if(fclose(f))failed=1;
    int result=failed ? -7:source_data_bubble_load(data,n);free(data);return result;
}
#endif
'''


def generate(text: str) -> dict:
    sections = parse_mesh(text)
    blob, layout, crc = make_blob(sections)
    storage = "\n".join(f"{s['ctype']} {s['name']}[{s['count']}] __attribute__((aligned(16)));" for s in layout)
    descriptors = "\n".join("    {"+",".join(map(str, (s['kind'], s['count'], s['offset'], s['bytes'])))+
                            ","+s['name']+"}," for s in layout)
    source = LOADER.replace("@STORAGE@", storage).replace("@SECTIONS@", descriptors)
    source = source.replace("@BYTES@", str(len(blob))).replace("@LAYOUT_CRC@", hex(crc))
    header = '''#ifndef JGR_SOURCE_DATA_BUBBLE_H
#define JGR_SOURCE_DATA_BUBBLE_H
#include <stddef.h>
int source_data_bubble_load(const void *image,size_t bytes);
int source_data_bubble_load_file(const char *path);
int source_data_bubble_ready(void);
#endif
'''
    declarations = ['/* External mutable storage prevents mesh constant folding. */',
                    '#include "source_data_bubble.h"']
    declarations += [f"extern {s['ctype']} {s['name']}[{s['count']}];" for s in layout]
    for typename, name, suffix in (("BubbleVertex", "bubble_vertices", "v"),
                                   ("unsigned short", "bubble_indices", "i"),
                                   ("BubbleBatch", "bubble_batches", "b")):
        declarations.append(f"static const {typename} *const {name}[]={{"+
                            ",".join(f"jgr_bubble_{suffix}{i}" for i in range(3))+"};")
    return {"blob": blob, "layout": layout,
            "source": {"source_data_bubble.c": source.encode(), "source_data_bubble.h": header.encode(),
                       "source_data_bubble.inc": ("\n".join(declarations)+"\n").encode()},
            "manifest": {"schema": 1, "kind": "spray_bubble_bss_prototype", "layout_crc32": f"{crc:08x}",
                         "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
                         "source_input_sha256": hashlib.sha256(text.encode()).hexdigest(),
                         "data_is_not_public": True,
                         "regenerated_from_disc": False,
                         "status": "generated-source conversion; upstream disc provenance must be verified separately"}}


def write_prototype(mesh: Path, out: Path) -> dict:
    if out.exists() or out.is_symlink():
        raise ValueError("Prototype output must be a new directory")
    result = generate(mesh.read_text())
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".bubble-prototype-", dir=out.parent) as temp:
        tree = Path(temp)/"tree"
        source = tree/"source/psp/src";source.mkdir(parents=True)
        private = tree/"PRIVATE-assets";private.mkdir()
        for name, content in result["source"].items():
            (source/name).write_bytes(content)
        (private/"spray_bubble.jsd").write_bytes(result["blob"])
        (tree/"prototype.json").write_text(json.dumps(result["manifest"] | {"sections": result["layout"]}, indent=2)+"\n")
        tree.rename(out)
    return result["manifest"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mesh", type=Path, required=True, help="Locally generated original mesh include")
    parser.add_argument("--out", type=Path, required=True, help="New isolated prototype directory")
    args = parser.parse_args()
    try:
        print(json.dumps(write_prototype(args.mesh, args.out), indent=2))
    except (OSError, ValueError, struct.error) as exc:
        parser.exit(1, f"source-data-blob: {exc}\n")


if __name__ == "__main__":
    main()
