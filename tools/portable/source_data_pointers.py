#!/usr/bin/env python3
"""Externalize reviewed compiler data, permitting only internal MIPS32 pointers.

The compiler sees the private disc-generated declarations. Public output contains
only ABI types/dimensions, zero-initialized storage and validated loader metadata.
Relocations to code, external symbols and unreviewed sections fail closed.
"""
from __future__ import annotations
import argparse, hashlib, json, re, struct, subprocess, tempfile, zlib
from pathlib import Path

RECIPES = Path(__file__).with_suffix('.json')


def mask(text):
    # Keep character positions/newlines intact for lexical declaration spans.
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)


def declarations(text):
    clean = mask(text); result=[]; end=0
    for match in re.finditer(r'(?m)^[ \t]*(?:static\s+)?const\s+', clean):
        if match.start()<end: continue
        at=match.end(); depth=0
        while at<len(clean):
            c=clean[at]
            if c=='=' and not depth: break
            depth += (c in '{([')-(c in '})]'); at+=1
        if at==len(clean): raise ValueError('Missing data initializer')
        prefix=text[match.start():at].strip()
        if '(' in re.sub(r'__attribute__\s*\(\(.*?\)\)', '', prefix):
            raise ValueError('Function or complex declarator in data input')
        stop=at+1; depth=0
        while stop<len(clean):
            c=clean[stop]
            if c==';' and not depth: break
            depth += (c in '{([')-(c in '})]'); stop+=1
        if stop==len(clean): raise ValueError('Unterminated data initializer')
        plain=re.sub(r'__attribute__\s*\(\(aligned\(\d+\)\)\)', '', prefix).strip()
        parsed=re.search(r'([A-Za-z_]\w*)\s*((?:\[[^]\n]*\]\s*)*)$', plain)
        if not parsed: raise ValueError('Unsupported data declarator: '+prefix)
        name,dims=parsed[1],re.findall(r'\[([^]]*)\]',parsed[2]); typ=plain[:parsed.start()].strip()
        typ=re.sub(r'^static\s+', '', typ)
        if not typ.startswith('const '): raise ValueError('Missing reviewed const declaration')
        # Drop only top-level const. Keep const pointees for pointer arrays.
        if '*' in typ: mutable=re.sub(r'\bconst\s*$', '',typ).rstrip()
        else: mutable=re.sub(r'^const\s+', '',typ)
        result.append(dict(name=name,type=typ,mutable=mutable,dims=dims,prefix=prefix,
                           initializer=text[at+1:stop],start=match.start(),end=stop+1))
        end=stop+1
    if len({r['name'] for r in result}) != len(result): raise ValueError('Duplicate data symbols')
    return result


def parse_elf(image, rows):
    if len(image)<52 or image[:7]!=b'\x7fELF\x01\x01\x01': raise ValueError('Expected little-endian ELF32')
    h=struct.unpack_from('<HHIIIIIHHHHHH',image,16)
    if h[0]!=1 or h[1]!=8: raise ValueError('Expected relocatable MIPS object')
    shoff,shsize,n,namesindex=h[5],h[10],h[11],h[12]
    if shsize!=40 or not n or namesindex>=n or shoff+40*n>len(image): raise ValueError('Invalid section table')
    hs=[struct.unpack_from('<10I',image,shoff+40*i) for i in range(n)]
    def content(index):
        s=hs[index]; off,size=s[4:6]
        if off>len(image) or size>len(image)-off: raise ValueError('Section exceeds ELF')
        return image[off:off+size]
    def string(b,i):
        if i>=len(b) or b.find(b'\0',i)<0: raise ValueError('Invalid ELF string')
        return b[i:b.find(b'\0',i)].decode('ascii')
    names=[string(content(namesindex),s[0]) for s in hs]
    symtabs={}; symbols={}
    for i,s in enumerate(hs):
        if s[1]!=2: continue
        if s[6]>=n or s[9]!=16 or s[5]%16: raise ValueError('Invalid symbol table')
        values=[]
        for off in range(0,s[5],16):
            no,value,size,info,other,idx=struct.unpack_from('<IIIBBH',content(i),off)
            sym=dict(name=string(content(s[6]),no),value=value,size=size,kind=info&15,index=idx)
            values.append(sym)
            if sym['name']: symbols[sym['name']]=sym
        symtabs[i]=values
    sections=[]; indexmap={}
    for r in rows:
        sym=symbols.get(r['name']); index=names.index('.jgr_asset_'+r['name'])
        if not sym or sym['index']!=index or sym['value']!=0 or not sym['size'] or sym['size']!=hs[index][5]:
            raise ValueError('Data symbol extent differs from dedicated section')
        align=max(16,hs[index][8])
        if align>4096 or align&(align-1): raise ValueError('Unsupported alignment')
        probe=content(names.index('.jgr_probe_'+r['name']))
        count=struct.unpack_from('<I',probe)[0]
        if not count: raise ValueError('Empty object')
        indexmap[index]=len(sections)
        sections.append(r|dict(data=bytearray(content(index)),bytes=sym['size'],alignment=align,count=count))
    relocations=[]
    # Only literal pools reached by an allowed data pointer are imported.
    for i,s in enumerate(hs):
        if s[1] not in (4,9) or s[7] not in indexmap or not s[5]: continue
        if s[1]!=9 or s[9]!=8 or s[5]%8 or s[6] not in symtabs: raise ValueError('Unsupported ELF relocation format')
        source=indexmap[s[7]]; data=sections[source]['data']
        for off in range(0,s[5],8):
            loc,info=struct.unpack_from('<II',content(i),off)
            symid,kind=info>>8,info&255
            if kind!=2 or symid>=len(symtabs[s[6]]) or loc%4 or loc+4>len(data): raise ValueError('Only aligned R_MIPS_32 data pointers are permitted')
            sym=symtabs[s[6]][symid]; targetindex=sym['index']
            if not 0<targetindex<n or sym['kind'] not in (0,1,3): raise ValueError('External/function relocation forbidden')
            if targetindex not in indexmap:
                section=hs[targetindex]
                if names[targetindex] not in ('.rodata','.rodata.str1.1','.rodata.str1.4') or section[1]!=1 or section[2]&5:
                    raise ValueError('Pointer target is outside reviewed data')
                if any(q[1] in (4,9) and q[7]==targetindex and q[5] for q in hs): raise ValueError('Literal pool contains relocations')
                indexmap[targetindex]=len(sections)
                sections.append(dict(name='literal_pool_'+str(targetindex),type='unsigned char',mutable='unsigned char',dims=[''],
                    count=section[5],data=bytearray(content(targetindex)),bytes=section[5],alignment=max(16,section[8]),literal=True))
            target=indexmap[targetindex]; addend=struct.unpack_from('<I',data,loc)[0]; offset=sym['value']+addend
            if offset>=sections[target]['bytes']: raise ValueError('Pointer target exceeds internal object')
            relocations.append((source,loc,target,offset)); data[loc:loc+4]=bytes(4)
    relocations.sort()
    if len({(a,b) for a,b,_,_ in relocations})!=len(relocations): raise ValueError('Duplicate pointer locations')
    return sections,relocations


def generate_group(text,preamble,compiler,name):
    if not re.fullmatch('[a-z][a-z0-9_]*',name): raise ValueError('Unsafe group name')
    rows=declarations(text)
    if not rows: raise ValueError('No declarations in group')
    macros='\n'.join(re.findall(r'(?m)^\s*#define[^\n]*',text))
    private=[preamble,macros]
    for r in rows:
        private.append(r['prefix']+f' __attribute__((used,section(".jgr_asset_{r["name"]}")))='+r['initializer']+';')
        count=f'sizeof({r["name"]})/sizeof({r["name"]}[0])' if r['dims'] else '1'
        private.append(f'const unsigned jgr_probe_{r["name"]} __attribute__((used,section(".jgr_probe_{r["name"]}")))={count};')
    with tempfile.TemporaryDirectory(prefix='jgr-private-pointers-') as temp:
        c,o=Path(temp)/'asset.c',Path(temp)/'asset.o'; c.write_text('\n'.join(private))
        subprocess.run([compiler,'-std=gnu99','-O0','-G0','-fno-merge-constants','-fno-merge-all-constants','-c',str(c),'-o',str(o)],check=True)
        sections,fixups=parse_elf(o.read_bytes(),rows)
    # Header40, section rows16, fixups16. Directory+fixups identity is compiled into loader.
    header=40+16*(len(sections)+len(fixups)); blob=bytearray((header+15)&~15); layout=[]
    for s in sections:
        blob.extend(bytes(-len(blob)%s['alignment'])); off=len(blob); blob.extend(s['data'])
        layout.append(dict(id=zlib.crc32((name+'/'+s['name']).encode()),offset=off,bytes=s['bytes'],alignment=s['alignment']))
    directory=b''.join(struct.pack('<4I',*r.values()) for r in layout)+b''.join(struct.pack('<4I',*r) for r in fixups)
    crc=zlib.crc32(directory); blob[40:header]=directory
    blob[:40]=struct.pack('<4s9I',b'JGSD',2,len(sections),len(blob),crc,zlib.crc32(blob[header:]),header,len(fixups),4,0)
    prefix='source_data_ptr_'+name; impl=['/* Payload-free compiler ABI and BSS storage. */','#if JGR_PORTABLE_ASSETS',preamble,macros,'#include "source_data_pointers.h"']
    guard='JGR_SOURCE_DATA_PTR_'+name.upper()+'_INC'
    decl=['/* Payload-free external storage and reviewed dispatch code. */','#ifndef '+guard,'#define '+guard]
    replacements=[]; entries=[]
    for s,r in zip(sections,layout):
        dims=list(s['dims'])
        if dims and not dims[0]: dims[0]=str(s['count'])
        suffix=''.join('['+d+']' for d in dims)
        symbol='jgr_ptr_'+name+'_'+s['name']
        declaration=s['mutable']+' '+s['name']+suffix+f' __asm__("{symbol}")'
        impl.append(declaration+f' __attribute__((aligned({r["alignment"]})));')
        if not s.get('literal'):
            replacement='extern '+declaration+';\n'+f'typedef char {symbol}_abi[(sizeof({s["name"]})=={r["bytes"]}u)?1:-1];'
            replacements.append((s['start'],s['end'],replacement))
        entries.append('    {'+','.join(str(x) for x in r.values())+','+ ('&' if not s['dims'] else '')+s['name']+'},')
    # Preserve only comments, macros and reviewed helper dispatch after replacing data.
    public=text
    for a,b,replacement in reversed(replacements): public=public[:a]+replacement+public[b:]
    decl.append(public);decl.append('#endif')
    impl+=['static const SourceDataPointerSection sections[]={',*entries,'};',
      f'int {prefix}_load(void){{return source_data_pointers_load_file("{prefix}.jsd",sections,{len(sections)}u,{len(blob)}u,0x{crc:08x}u,{len(fixups)}u);}}','#endif']
    return dict(blob=bytes(blob),source={prefix+'.c':'\n'.join(impl)+'\n',prefix+'.inc':'\n'.join(decl)+'\n'},layout=layout,fixups=fixups,
      manifest=dict(kind='compiler_internal_pointer_sections_v2',name=name,source_sha256=hashlib.sha256(text.encode()).hexdigest(),preamble_sha256=hashlib.sha256(preamble.encode()).hexdigest(),
                    blob_sha256=hashlib.sha256(blob).hexdigest(),bytes=len(blob),sections=len(sections),relocations=len(fixups)))


def generate(source_dir,out,compiler,recipes=RECIPES):
    source_dir,out=Path(source_dir),Path(out).absolute(); document=json.loads(Path(recipes).read_text())
    if document.get('schema')!=1: raise ValueError('Unknown pointer recipe schema')
    if out.exists() or out.is_symlink() or any(p.is_symlink() for p in out.parents): raise ValueError('Output must be new and not linked')
    missing=[r['input'] for r in document['groups'] if not (source_dir/r['input']).is_file()]
    if missing: raise ValueError('Missing generated disc inputs: '+','.join(missing))
    out.parent.mkdir(parents=True,exist_ok=True); reports=[]
    with tempfile.TemporaryDirectory(prefix='.pointer-assets-',dir=out.parent) as temp:
        tree=Path(temp)/'tree'; public=tree/'source/psp/src'; private=tree/'PRIVATE-assets'; public.mkdir(parents=True); private.mkdir()
        calls=['#include "source_data_pointers.h"','#if JGR_PORTABLE_ASSETS']
        for row in document['groups']:
            result=generate_group((source_dir/row['input']).read_text(),row['preamble'],compiler,row['name'])
            for filename,data in result['source'].items(): (public/filename).write_text(data)
            (private/('source_data_ptr_'+row['name']+'.jsd')).write_bytes(result['blob'])
            calls.append(f'int source_data_ptr_{row["name"]}_load(void);')
            reports.append(result['manifest']|dict(input=row['input'],layout=result['layout']))
        calls.append('int source_data_pointers_load(void)\n{')
        for row in document['groups']: calls.append(f'    if(source_data_ptr_{row["name"]}_load()<0)return -1;')
        calls+=['    return 0;','}','#endif']
        (public/'source_data_pointers_all.c').write_text('\n'.join(calls)+'\n')
        receipt=dict(schema=1,groups=reports,contains_private_game_data=True,source_is_payload_free=True)
        (tree/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');tree.rename(out)
    return receipt


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--cc',required=True);a=p.parse_args()
    try:
        result=generate(a.source_dir,a.out,a.cc);print(f"Generated {len(result['groups'])} internal-pointer asset groups")
    except (ValueError,OSError,subprocess.CalledProcessError) as e:p.exit(1,f'source-data-pointers: {e}\n')
if __name__=='__main__':main()
