#!/usr/bin/env node
/* Genuine Chromium File System Access tests in an isolated OPFS directory.
 * No user files, user browser profiles, npm packages, or game data required. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const args = Object.fromEntries(Array.from({length: (process.argv.length - 2) / 2}, (_, i) => [process.argv[2 + i * 2].replace(/^--/, ''), process.argv[3 + i * 2]]));
if (!args.out) throw Error('Usage: test_filesystem.mjs --out fresh-directory [--chrome executable]');
const out = path.resolve(args.out);
if (fs.existsSync(out) && (await fsp.readdir(out)).length) throw Error('Output directory must be absent or empty.');
await fsp.mkdir(out, {recursive: true});
const chromePath = args.chrome || process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const source = (await Promise.all(['filesystem.js', 'background.js'].map(name => fsp.readFile(path.join(here, name), 'utf8')))).join('\n');
async function browserTests() {
  const root = await navigator.storage.getDirectory();
  const suiteName = 'jgr-filesystem-' + crypto.randomUUID();
  const suite = await root.getDirectoryHandle(suiteName, {create: true});
  const results = [], ios = [];
  const bytes = text => new TextEncoder().encode(text);
  const text = value => new TextDecoder().decode(value);
  const equal = (actual, expected, message = '') => {if (JSON.stringify(actual) !== JSON.stringify(expected)) throw Error(`${message}: ${JSON.stringify(actual)} !== ${JSON.stringify(expected)}`);};
  const check = (condition, message) => {if (!condition) throw Error(message);};
  async function rejects(operation, errno) {
    try {await operation();} catch (error) {
      const actual = new JgrBrowserIO(suite).translate(error).errno;
      equal(actual, errno, 'error number'); return;
    }
    throw Error(`Operation succeeded; expected errno ${errno}.`);
  }
  async function run(name, body) {
    try {await body(); results.push({name, passed: true});}
    catch (error) {results.push({name, passed: false, error: error.stack || String(error)});}
  }
  async function fixture(override, sources = []) {
    const directory = await suite.getDirectoryHandle('case-' + crypto.randomUUID(), {create: true});
    const io = new JgrBrowserIO(override ? override(directory) : directory, sources);
    ios.push(io);
    for (const name of ['data', 'data/assets', 'data/cache', 'data/setup']) await io.mkdir('/game/' + name);
    return {io, directory};
  }
  async function write(io, name, value, mode = 'wb') {const fd = await io.open('/game/' + name, mode); await io.write(fd, bytes(value)); await io.close(fd);}
  async function read(io, name) {const fd = await io.open('/game/' + name, 'rb'); const value = text(await io.read(fd, 1024)); await io.close(fd); return value;}
  function faultStream(fault) {
    const stream = new WritableStream({
      write() {if(fault !== 'close') throw new DOMException('Injected output fault', 'QuotaExceededError');},
      close() {if(fault === 'close') throw new DOMException('Injected close fault', 'QuotaExceededError');},
    });
    // FileSystemWritableFileStream adds these convenience methods to a stream.
    for(const method of ['write','close','abort']) stream[method] = async value => {
      const writer = stream.getWriter();
      try {return await writer[method](value);} finally {writer.releaseLock();}
    };
    return stream;
  }
  function wrapped(directory, fault) {
    return {
      getDirectoryHandle: async (name, options) => wrapped(await directory.getDirectoryHandle(name, options), fault),
      getFileHandle: async (name, options) => {
        const file = await directory.getFileHandle(name, options);
        return {getFile: () => file.getFile(), move: undefined,
          createWritable: options => fault && name === 'failed.bin'
            ? Promise.resolve(faultStream(fault))
            : file.createWritable(options)};
      },
      removeEntry: (name, options) => directory.removeEntry(name, options),
      entries: () => directory.entries(),
    };
  }
  async function artworkFile(imageWidth = 480, imageHeight = 272) {
    const canvas = new OffscreenCanvas(imageWidth, imageHeight), context = canvas.getContext('2d');
    const pixels = context.createImageData(imageWidth, imageHeight);
    pixels.data.set([255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255, 255,0,0,0, 255,0,0,128]);
    context.putImageData(pixels, 0, 0);
    const png = new Uint8Array(await (await canvas.convertToBlob({type:'image/png'})).arrayBuffer());
    const header = new Uint8Array(40), view = new DataView(header.buffer);
    header.set([0,80,66,80]); view.setUint32(4, 0x10000, true);
    for (let i = 0; i < 8; ++i) view.setUint32(8 + 4*i, i <= 4 ? 40 : 40 + png.length, true);
    return new File([header, png], 'EBOOT.PBP');
  }
  async function outputImage(io) {
    const fd = await io.open('/game/data/setup/background.jbg', 'rb');
    try { return await io.read(fd, 300000); } finally { await io.close(fd); }
  }
  function referenceCrc(data) {
    let value = 0xffffffff;
    for (const byte of data) {
      value ^= byte;
      for (let bit = 0; bit < 8; ++bit) value = (value >>> 1) ^ (value & 1 ? 0xedb88320 : 0);
    }
    return (value ^ 0xffffffff) >>> 0;
  }
  await run('PIC1 converts to exact bounded JBG header, RGB565 and CRC', async () => {
    const {io} = await fixture(null, [await artworkFile()]);
    equal(await jgrPrepareBackground(io), true);
    const image = await outputImage(io), words = new DataView(image.buffer, image.byteOffset, image.byteLength);
    equal(image.length, 24 + 480*272*2); equal(text(image.subarray(0, 8)), 'JGRBG01\0');
    equal([8,12,16].map(at => words.getUint32(at, true)), [480,272,480*272*2]);
    equal(words.getUint32(20, true), referenceCrc(image.subarray(24)));
    equal([0,1,2,3,4,5].map(i => words.getUint16(24 + i*2, true)), [31,2016,63488,65535,0,16]);
    equal(io.files.size, 0);
    equal((await io.list('/game/data/setup')).map(entry => entry.name), ['background.jbg']);
  });
  await run('absent PIC1 is supported and preserves existing optional artwork', async () => {
    const header = new Uint8Array(40); header.set([0,80,66,80]);
    for (let i = 0; i < 8; ++i) new DataView(header.buffer).setUint32(8 + i*4, 40, true);
    const {io} = await fixture(null, [new File([header], 'EBOOT.PBP')]);
    await write(io, 'data/setup/background.jbg', 'prior');
    equal(await jgrPrepareBackground(io), false); equal(await read(io, 'data/setup/background.jbg'), 'prior');
    equal(io.files.size, 0);
  });
  await run('invalid PIC1 dimensions, PNG CRC and PBP ranges leave prior artwork intact', async () => {
    const good = new Uint8Array(await (await artworkFile()).arrayBuffer());
    const badCrc = good.slice(); badCrc[good.length-1] ^= 1;
    const badPng = good.slice(); badPng[40] = 0;
    const badRange = good.slice(); new DataView(badRange.buffer).setUint32(8 + 5*4, good.length + 1, true);
    for (const input of [await artworkFile(479,272), new File([badCrc], 'EBOOT.PBP'), new File([badPng], 'EBOOT.PBP'), new File([badRange], 'EBOOT.PBP')]) {
      const {io} = await fixture(null, [input]); await write(io, 'data/setup/background.jbg', 'prior');
      let rejected = false; try { await jgrPrepareBackground(io); } catch (_) { rejected = true; }
      check(rejected, 'Invalid artwork was accepted.'); equal(await read(io, 'data/setup/background.jbg'), 'prior');
      equal(io.files.size, 0); equal((await io.list('/game/data/setup')).map(entry => entry.name), ['background.jbg']);
    }
  });
  await run('artwork write failure does not publish a partial image', async () => {
    const {io} = await fixture(null, [await artworkFile()]); await write(io, 'data/setup/background.jbg', 'prior');
    const original = io.write.bind(io);
    io.write = (fd, data) => io.entry(fd).path.endsWith('background.jbg.tmp') ? Promise.reject(io.error(28, 'Injected full destination')) : original(fd, data);
    await rejects(() => jgrPrepareBackground(io), 28);
    equal(await read(io, 'data/setup/background.jbg'), 'prior'); equal(io.files.size, 0);
    equal((await io.list('/game/data/setup')).map(entry => entry.name), ['background.jbg']);
  });
  await run('create, bounded write/read, seek and zero-filled gap', async () => {
    const {io} = await fixture();
    const fd = await io.open('/game/data/assets/a.bin', 'w+b');
    check(fd >= 1000, 'Browser descriptors overlap low native descriptors.');
    equal(await io.write(fd, bytes('abcd')), 4);
    equal(await io.seek(fd, 1, 0), 1); await io.write(fd, bytes('Z'));
    await io.seek(fd, 6, 0); await io.write(fd, bytes('!'));
    equal((await io.statFd(fd)).size, 7);
    await io.seek(fd, 0, 0); equal(Array.from(await io.read(fd, 20)), [97,90,99,100,0,0,33]);
    equal((await io.read(fd, 1)).length, 0);
    await io.close(fd); await rejects(() => io.read(fd, 1), 9);
  });
  await run('r+ overwrites, w truncates, append honors EOF after seek', async () => {
    const {io} = await fixture(); await write(io, 'data/assets/a.bin', 'abc');
    const update = await io.open('/game/data/assets/a.bin', 'r+b');
    await io.seek(update, 1, 0); await io.write(update, bytes('Z')); await io.close(update);
    equal(await read(io, 'data/assets/a.bin'), 'aZc');
    const trunc = await io.open('/game/data/assets/a.bin', 'wb'); await io.close(trunc);
    equal((await io.stat('/game/data/assets/a.bin')).size, 0);
    await write(io, 'data/assets/a.bin', 'q');
    const append = await io.open('/game/data/assets/a.bin', 'a+b');
    await io.seek(append, 0, 0); await io.write(append, bytes('!')); await io.seek(append, 0, 0);
    equal(text(await io.read(append, 8)), 'q!'); await io.close(append);
    await rejects(() => io.open('/game/data/assets/missing.bin', 'r+b'), 2);
  });
  await run('existing reader refreshes size, content, and SEEK_END after another writer', async () => {
    const {io} = await fixture(); await write(io, 'data/assets/a.bin', 'ab');
    const reader = await io.open('/game/data/assets/a.bin', 'rb');
    const writer = await io.open('/game/data/assets/a.bin', 'r+b');
    await io.seek(writer, 0, 2); await io.write(writer, bytes('cd')); await io.close(writer);
    equal(text(await io.read(reader, 9)), 'abcd');
    equal((await io.statFd(reader)).size, 4, 'refreshed descriptor size');
    equal(await io.seek(reader, 0, 2), 4, 'refreshed SEEK_END');
    equal((await io.stat('/game/data/assets/a.bin')).size, 4, 'path size');
    await io.close(reader);
  });
  await run('pending writer metadata takes precedence over older reader snapshot', async () => {
    const {io} = await fixture(); await write(io, 'data/assets/a.bin', 'ab');
    const reader = await io.open('/game/data/assets/a.bin', 'rb');
    const writer = await io.open('/game/data/assets/a.bin', 'r+b');
    await io.seek(writer, 0, 2); await io.write(writer, bytes('cd'));
    equal((await io.stat('/game/data/assets/a.bin')).size, 4);
    await io.close(writer); await io.close(reader);
  });
  await run('multiple append descriptors preserve both writes', async () => {
    const {io} = await fixture(); await write(io, 'data/assets/a.bin', 'a');
    const first = await io.open('/game/data/assets/a.bin', 'ab');
    const second = await io.open('/game/data/assets/a.bin', 'ab');
    await io.write(first, bytes('b')); await io.write(second, bytes('c'));
    await io.close(first); await io.close(second);
    equal(await read(io, 'data/assets/a.bin'), 'abc');
  });
  await run('readonly disc overlay and source/external path mutations are denied', async () => {
    const {io} = await fixture(null, [new File(['disc-data'], 'disc.gdi')]);
    equal(await read(io, 'disc.gdi'), 'disc-data');
    for (const name of ['/game/disc.gdi','/game/EBOOT.PBP','/PSP/SAVEDATA/JGR_PORTABLE/jgr_story.sav','/game/data/assets/../evil','/outside','/gameish/data/assets/x']) {
      await rejects(() => io.open(name, 'wb'), 13);
      await rejects(() => io.remove(name), 13);
      await rejects(() => io.rename('/game/data/assets/a.bin', name), 13);
    }
    await rejects(() => io.open('/game/data/assets/evil\0name', 'wb'), 22);
    await rejects(() => io.open('/game/data/assets/evil\\name', 'wb'), 22);
    equal(await read(io, 'disc.gdi'), 'disc-data');
    equal((await io.stat('/game/disc.gdi')).mode & 0o777, 0o444);
  });
  await run('file operations enforce mode, invalid seeks and missing entries', async () => {
    const {io} = await fixture(); await write(io, 'data/assets/a.bin', 'a');
    const reader = await io.open('/game/data/assets/a.bin', 'rb');
    await rejects(() => io.write(reader, bytes('x')), 9);
    await rejects(() => io.seek(reader, -1, 0), 22);
    await rejects(() => io.seek(reader, 0, 4), 22);
    const writer = await io.open('/game/data/assets/b.bin', 'wb');
    await rejects(() => io.read(writer, 1), 9);
    await rejects(() => io.remove('/game/data/assets/a.bin'), 13);
    await rejects(() => io.rename('/game/data/assets/a.bin', '/game/data/assets/c.bin'), 13);
    await io.close(reader); await io.close(writer);
    await rejects(() => io.stat('/game/data/assets/missing'), 2);
  });
  await run('directory listing, mkdir EEXIST, nested removal and recreation', async () => {
    const {io} = await fixture(null, [new File(['x'], 'disc.gdi')]);
    equal((await io.list('/game')).map(x => x.name), ['data', 'disc.gdi']);
    await rejects(() => io.mkdir('/game/data/assets'), 17);
    await io.mkdir('/game/data/assets/sub'); await write(io, 'data/assets/sub/file.bin', 'x');
    equal((await io.list('/game/data/assets/sub')).map(x => x.name), ['file.bin']);
    await rejects(() => io.remove('/game/data/assets/sub'), 39);
    await io.remove('/game/data/assets/sub/file.bin'); await io.remove('/game/data/assets/sub');
    await rejects(() => io.stat('/game/data/assets/sub'), 2);
    await io.mkdir('/game/data/assets/sub'); equal((await io.list('/game/data/assets/sub')).length, 0);
  });
  await run('native publish preserves data and removes temporary', async () => {
    const {io} = await fixture(); await write(io, 'data/assets/tmp.bin', 'payload');
    await io.rename('/game/data/assets/tmp.bin', '/game/data/assets/final.bin');
    equal(await read(io, 'data/assets/final.bin'), 'payload');
    await rejects(() => io.stat('/game/data/assets/tmp.bin'), 2);
  });
  await run('streamed publish fallback replaces destination exactly', async () => {
    const {io} = await fixture(directory => wrapped(directory, false));
    await write(io, 'data/assets/tmp.bin', 'new'); await write(io, 'data/assets/final.bin', 'old-long-value');
    await io.rename('/game/data/assets/tmp.bin', '/game/data/assets/final.bin');
    equal(await read(io, 'data/assets/final.bin'), 'new');
    await rejects(() => io.stat('/game/data/assets/tmp.bin'), 2);
  });
  await run('publish failure keeps source and never produces completion marker', async () => {
    const {io} = await fixture(directory => wrapped(directory, true));
    await write(io, 'data/assets/tmp.bin', 'keep-source');
    await rejects(() => io.rename('/game/data/assets/tmp.bin', '/game/data/assets/failed.bin'), 28);
    equal(await read(io, 'data/assets/tmp.bin'), 'keep-source');
    await rejects(() => io.stat('/game/data/cache/assets.checked'), 2);
  });
  await run('failed write reports error, clears descriptor and leaves previous file intact', async () => {
    const {io, directory} = await fixture(directory => wrapped(directory, 'write'));
    const data = await directory.getDirectoryHandle('data');
    const assets = await data.getDirectoryHandle('assets');
    const handle = await assets.getFileHandle('failed.bin', {create:true});
    const stream = await handle.createWritable(); await stream.write('original'); await stream.close();
    const fd = await io.open('/game/data/assets/failed.bin', 'wb');
    await rejects(() => io.write(fd, bytes('replacement')), 28);
    await rejects(() => io.close(fd), 5);
    equal(io.files.size, 0);
    equal(await (await handle.getFile()).text(), 'original');
  });
  await run('failed commit reports error and closes descriptor', async () => {
    const {io} = await fixture(directory => wrapped(directory, 'close'));
    const fd = await io.open('/game/data/assets/failed.bin', 'wb');
    await io.write(fd, bytes('replacement'));
    await rejects(() => io.close(fd), 28);
    equal(io.files.size, 0);
    await rejects(() => io.stat('/game/data/cache/assets.checked'), 2);
  });
  await run('small random source reads stay bounded to requested bytes', async () => {
    const chunk = new Uint8Array(65536); chunk.fill(42);
    const file = new File(Array(256).fill(chunk), 'bulk.bin');
    const {io} = await fixture(null, [file]);
    const original = io.reader; const requests = [];
    io.reader = {readAsArrayBuffer(blob) {requests.push(blob.size); return original.readAsArrayBuffer(blob);}};
    const fd = await io.open('/game/bulk.bin', 'rb');
    await io.seek(fd, file.size - 8, 0); equal(Array.from(await io.read(fd, 8)), Array(8).fill(42));
    await io.seek(fd, 3, 0); equal((await io.read(fd, 5)).length, 5);
    equal(requests, [8, 5]); equal(io.bytesRead, 13); await io.close(fd);
  });
  for (const io of ios) try {await io.closeAll();} catch (error) {results.push({name:'descriptor cleanup',passed:false,error:String(error)});}
  await root.removeEntry(suiteName, {recursive: true});
  postMessage({type: 'done', results, passed: results.every(test => test.passed)});
}
const worker = `${source}\n(${browserTests.toString()})().catch(error=>postMessage({type:'error',message:error.stack||String(error)}));`;
const html = '<!doctype html><meta charset="utf-8"><script>window.result=null;const worker=new Worker(URL.createObjectURL(new Blob([' + JSON.stringify(worker).replaceAll('</', '<\\/') + '],{type:"text/javascript"})));worker.onmessage=e=>window.result=e.data;worker.onerror=e=>window.result={type:"error",message:e.message};</script>';
let chrome, socket, server;
try {
  server = http.createServer((request, response) => {response.setHeader('Content-Type','text/html');response.end(html);});
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const profile = path.join(out, 'profile');
  const log = fs.openSync(path.join(out, 'chrome.log'), 'w');
  chrome = spawn(chromePath, ['--headless=new','--remote-debugging-port=0',`--user-data-dir=${profile}`,'--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-extensions',`http://127.0.0.1:${server.address().port}/`],{stdio:['ignore',log,log]});fs.closeSync(log);
  const sleep = ms => new Promise(resolve=>setTimeout(resolve,ms));
  for(let i=0;!fs.existsSync(path.join(profile,'DevToolsActivePort'))&&i<200;++i)await sleep(100);
  const port=(await fsp.readFile(path.join(profile,'DevToolsActivePort'),'utf8')).split('\n')[0];
  const tabs=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  socket=new WebSocket(tabs.find(tab=>tab.type==='page').webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
  const waiting=new Map();let sequence=0;
  socket.onmessage=({data})=>{const message=JSON.parse(data), pending=waiting.get(message.id);if(pending){waiting.delete(message.id);message.error?pending.reject(message.error):pending.resolve(message.result);}};
  const send=(method,params={})=>new Promise((resolve,reject)=>{const id=++sequence;waiting.set(id,{resolve,reject});socket.send(JSON.stringify({id,method,params}));});
  let result;
  for(let i=0;i<1200;i++) {result=(await send('Runtime.evaluate',{expression:'window.result',returnByValue:true})).result.value;if(result)break;await sleep(100);}
  assert(result,'Filesystem browser tests timed out.');
  result.browser=await send('Browser.getVersion');
  await fsp.writeFile(path.join(out,'results.json'),JSON.stringify(result,null,2)+'\n');
  for(const test of result.results||[])console.log(`${test.passed?'PASS':'FAIL'} ${test.name}${test.error?'\n'+test.error:''}`);
  assert.equal(result.type,'done',result.message);assert.equal(result.passed,true,'Filesystem tests failed.');
  console.log(`PASS: ${result.results.length} browser filesystem tests.`);
} finally {socket?.close();chrome?.kill('SIGTERM');server?.close();}
