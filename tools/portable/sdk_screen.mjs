// Read the SDK debug screen's CPU-written 8888 framebuffer in an isolated
// emulator. It has no GE render target, so gpu.buffer.renderColor is unsuitable.
import {writeFile} from 'node:fs/promises';
const [port,path]=process.argv.slice(2);
if(!/^\d+$/.test(port ?? '') || !path)throw Error('usage: sdk_screen.mjs PORT OUTPUT.raw');
const ws=new WebSocket(`ws://127.0.0.1:${port}/debugger`);
let complete=false;
let offset=0;const parts=[],size=512*272*4;
const timer=setTimeout(()=>{console.error('SDK framebuffer read timed out');process.exit(1);},10000);
function readNext(){ws.send(JSON.stringify({event:'memory.read',address:0x04000000+offset,size:Math.min(16384,size-offset),ticket:'jgr-sdk-screen'}));}
ws.addEventListener('open',readNext);
ws.addEventListener('error',()=>{if(complete)return;clearTimeout(timer);console.error('SDK framebuffer connection failed');process.exit(1);});
ws.addEventListener('message',async({data})=>{
    const r=JSON.parse(data);
    if(r.ticket==='jgr-sdk-drain'){clearTimeout(timer);ws.close();return;}
    if(r.ticket!=='jgr-sdk-screen')return;
    try {
        if(r.event==='error')throw Error(JSON.stringify(r));
        if(typeof r.base64!=='string')throw Error('Unexpected memory.read keys: '+Object.keys(r).join(','));
        const pixels=Buffer.from(r.base64,'base64');
        if(pixels.length!==Math.min(16384,size-offset))throw Error('Unexpected framebuffer size: '+pixels.length);
        parts.push(pixels);offset+=pixels.length;
        if(offset<size){readNext();return;}
        complete=true;
        await writeFile(path,Buffer.concat(parts));console.log(JSON.stringify({path,bytes:size,source:'SDK VRAM 04000000'}));
        // Drain a small response before closing: avoid tearing down the host
        // debugger while a large base64 memory response is being finalized.
        ws.send(JSON.stringify({event:'cpu.status',ticket:'jgr-sdk-drain'}));
    } catch(e){clearTimeout(timer);console.error(e.message);process.exitCode=1;ws.close();}
});
