// Read PPSSPP's rendered display, not stale emulated VRAM in hardware mode.
// Briefly steps an isolated test emulator, reads its display, then resumes it.
// No controller input, memory writes, or save operations. Node 22+ WebSocket.
import {writeFile} from 'node:fs/promises';
const [port,path]=process.argv.slice(2);
if(!/^\d+$/.test(port ?? '') || !path)throw Error('usage: node ppsspp_gpu_capture.mjs LOCAL_PORT OUTPUT.png');
const ws=new WebSocket(`ws://127.0.0.1:${port}/debugger`);
let serial=0,pausedHere=false;
const pending=new Map();
const send=event=>ws.send(JSON.stringify({event}));
function request(event){
    return new Promise((resolve,reject)=>{
        const ticket=`jgr-render-${++serial}`;
        const timer=setTimeout(()=>{pending.delete(ticket);reject(Error(`${event} timed out`));},3000);
        pending.set(ticket,{resolve,reject,timer});ws.send(JSON.stringify({event,ticket}));
    });
}
ws.addEventListener('message',({data})=>{
    const r=JSON.parse(data),p=pending.get(r.ticket);if(!p)return;
    clearTimeout(p.timer);pending.delete(r.ticket);
    if(r.event==='error')p.reject(Error(JSON.stringify(r)));else p.resolve(r);
});
try {
    await new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>reject(Error('GPU capture connection timed out')),3000);
        ws.addEventListener('open',()=>{clearTimeout(timer);resolve();},{once:true});
        ws.addEventListener('error',()=>{clearTimeout(timer);reject(Error('GPU capture connection failed'));},{once:true});
    });
    const initial=await request('cpu.status');
    if(!initial.stepping){
        if(initial.paused)throw Error('Emulator was already paused outside the debugger; leaving it unchanged');
        pausedHere=true;send('cpu.stepping');
        let ready=false;
        for(let i=0;i<40;++i){
            if((await request('cpu.status')).stepping){ready=true;break;}
            await new Promise(r=>setTimeout(r,50));
        }
        if(!ready)throw Error('Emulator did not enter debugger stepping');
    }
    let r,source='gpu.buffer.screenshot';
    try{r=await request(source);}catch(e){
        // MoltenVK cannot download the window's output on some PPSSPP
        // builds. Read the current PSP render target instead, and report
        // that distinction (a debugger stop can be mid-frame).
        if(!e.message.includes('Could not download output'))throw e;
        source='gpu.buffer.renderColor';r=await request(source);
    }
    const encoded=r.data ?? r.uri ?? r.url;
    if(typeof encoded!=='string')throw Error(JSON.stringify(r));
    const png=Buffer.from(encoded.replace(/^data:image\/png;base64,/,''),'base64');
    if(png.subarray(0,8).toString('hex')!=='89504e470d0a1a0a')
        throw Error(JSON.stringify({event:r.event,keys:Object.keys(r),width:r.width,height:r.height,format:r.format,bytes:png.length}));
    await writeFile(path,png);console.log(JSON.stringify({path,source,bytes:png.length,width:r.width,height:r.height}));
} catch(e){console.error(e.message);process.exitCode=1;}
finally {
    if(pausedHere && ws.readyState===WebSocket.OPEN){
        send('cpu.resume');
        try{if((await request('cpu.status')).stepping)throw Error('Emulator did not resume');}
        catch(e){console.error(e.message);process.exitCode=1;}
    }
    ws.close();
}
