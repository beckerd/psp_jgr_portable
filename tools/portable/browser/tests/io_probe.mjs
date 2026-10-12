/* Node test backend for the real Wasm/Asyncify stdio adapter. Browser folder
 * permissions and FileSystemWritableFileStream are tested separately. */
import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const createModule=require(path.resolve(process.argv[2]));
const root=await fs.mkdtemp(path.join(os.tmpdir(),'jgr-browser-io-'));
const entries=new Map();let next=1000;
const location=name=>{if(name==='/game')return root;if(!name.startsWith('/game/'))throw Object.assign(new Error('outside'),{errno:13});return path.join(root,name.slice(6));};
const codes={ENOENT:2,EIO:5,EBADF:9,EACCES:13,EEXIST:17,ENOTDIR:20,EISDIR:21,EINVAL:22,ENOSPC:28,ENOTEMPTY:39};
const statResult=st=>({size:st.size,mode:st.mode,mtime:st.mtimeMs/1000});
const io={
    translate:error=>({errno:error.errno>0?error.errno:(codes[error.code]||5)}),
    async open(name,mode){const handle=await fs.open(location(name),mode.replace('b',''));const st=await handle.stat();const fd=next++;entries.set(fd,{name,handle,append:mode.startsWith('a'),position:mode.startsWith('a')?st.size:0});return fd;},
    async read(fd,bytes){const e=entries.get(fd),out=new Uint8Array(bytes);const result=await e.handle.read(out,0,bytes,e.position);e.position+=result.bytesRead;return out.slice(0,result.bytesRead);},
    async write(fd,bytes){const e=entries.get(fd);if(e.name.endsWith('/full.bin'))throw {errno:28};if(e.append)e.position=(await e.handle.stat()).size;const result=await e.handle.write(bytes,0,bytes.length,e.position);e.position+=result.bytesWritten;return result.bytesWritten;},
    async seek(fd,offset,whence){const e=entries.get(fd);const next=(whence===0?0:whence===1?e.position:whence===2?(await e.handle.stat()).size:NaN)+offset;if(!Number.isSafeInteger(next)||next<0)throw Object.assign(new Error('seek'),{errno:22});e.position=next;return next;},
    async close(fd){const e=entries.get(fd);entries.delete(fd);await e.handle.close();if(e.name.endsWith('/close-error.bin'))throw {errno:5};},
    async stat(name){return statResult(await fs.stat(location(name)));},
    async statFd(fd){return statResult(await entries.get(fd).handle.stat());},
    async mkdir(name){await fs.mkdir(location(name));},
    async remove(name){await fs.unlink(location(name));},
    async rename(from,to){await fs.rename(location(from),location(to));},
    async list(name){return Promise.all((await fs.readdir(location(name))).map(async entry=>({name:entry,mode:(await fs.stat(path.join(location(name),entry))).mode})));}
};
try {
    const module=await createModule({browserIo:io,noInitialRun:true});
    const status=await module.ccall('main','number',[],[],{async:true});
    if(status)throw new Error('Probe failed: '+status);
    if(entries.size)throw new Error('The converter leaked file handles');
} finally {
    for(const e of entries.values())await e.handle.close().catch(()=>{});
    await fs.rm(root,{recursive:true,force:true});
}
