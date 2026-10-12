#!/usr/bin/env node
/* Run the embedded production worker in isolated desktop Chrome using real
 * input Files and a fresh OPFS directory. OPFS is a test destination only;
 * shipping file:// preparation uses the user's selected folder directly.
 * Node 22+; no npm dependencies. Never touches an existing browser profile. */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {pipeline} from 'node:stream/promises';

const options = {};
for (let i = 2; i < process.argv.length; i += 2) {
  const key = process.argv[i];
  if (!key?.startsWith('--') || !process.argv[i + 1]) throw Error('Expected --option value');
  options[key.slice(2)] = process.argv[i + 1];
}
if (options['save-sentinel'] && options['save-sentinel'] !== 'yes') throw Error('--save-sentinel accepts yes');
if (options['capture-deleted'] && !/^[A-Za-z0-9_.-]+\.setup-tmp$/.test(options['capture-deleted'])) {
  throw Error('--capture-deleted must be one generated temporary filename.');
}
for (const key of ['html', 'eboot', 'gdi', 'expected', 'out']) {
  if (!options[key]) throw Error('Usage: test_browser.mjs --html prepare.html --eboot EBOOT.PBP --gdi image.gdi --expected assets --out fresh-directory [--chrome executable] [--timeout seconds]');
  options[key] = path.resolve(options[key]);
}
const candidates = [options.chrome, process.env.CHROME,
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser',
  process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, 'Google/Chrome/Application/chrome.exe'),
].filter(Boolean);
const chromePath = candidates.find(p => fs.existsSync(p));
if (!chromePath) throw Error('Chrome not found; pass --chrome with its executable path.');
if (fs.existsSync(options.out) && (await fsp.readdir(options.out)).length) throw Error('--out must be absent or empty; existing test evidence is never overwritten.');
await fsp.mkdir(options.out, {recursive: true});
const prepared = path.join(options.out, 'prepared');
await fsp.mkdir(prepared);
const html = await fsp.readFile(options.html);
const digest = data => createHash('sha256').update(data).digest('hex');
async function fileHash(filename) {
  const hash = createHash('sha256');
  for await (const chunk of fs.createReadStream(filename)) hash.update(chunk);
  return hash.digest('hex');
}
async function collect(root, prefix = '') {
  const files = [];
  for (const item of await fsp.readdir(path.join(root, prefix), {withFileTypes: true})) {
    const name = prefix ? `${prefix}/${item.name}` : item.name;
    if (item.isSymbolicLink()) throw Error(`Unexpected symlink: ${name}`);
    if (item.isDirectory()) files.push(...await collect(root, name));
    else if (item.isFile()) files.push(name);
  }
  return files.sort();
}
function trackFiles(text) {
  const lines = text.trim().split(/\r?\n/);
  const count = Number(lines.shift());
  if (!Number.isInteger(count) || count < 1 || count !== lines.length) throw Error('Invalid GDI track count.');
  return lines.map(line => {
    const match = line.match(/^\s*\d+\s+\d+\s+\d+\s+\d+\s+(?:"([^"\r\n]+)"|(\S+))\s+\d+\s*$/);
    const name = match && (match[1] || match[2]);
    if (!name || name === '.' || name === '..' || /[\\/\0]/.test(name)) throw Error('The test requires safe, adjacent GDI track filenames.');
    return path.join(path.dirname(options.gdi), name);
  });
}
const inputs = [options.eboot, options.gdi, ...trackFiles(await fsp.readFile(options.gdi, 'utf8'))];
assert.equal(path.basename(options.eboot).toUpperCase(), 'EBOOT.PBP');
assert.equal(new Set(inputs.map(p => path.basename(p).toLowerCase())).size, inputs.length, 'Input filenames collide.');
for (const filename of inputs) assert((await fsp.stat(filename)).isFile(), `Missing input: ${filename}`);
const expectedNames = await collect(options.expected);
const expected = new Map();
for (const name of expectedNames) expected.set(name, {bytes: (await fsp.stat(path.join(options.expected, name))).size, sha256: await fileHash(path.join(options.expected, name))});
const receipt = {schema: 1, started: new Date().toISOString(), platform: `${os.platform()} ${os.arch()}`,
  html_sha256: digest(html), eboot_sha256: await fileHash(options.eboot),
  input_files: await Promise.all(inputs.map(async p => ({name: path.basename(p), bytes: (await fsp.stat(p)).size}))),
  expected_files: expected.size, expected_bytes: [...expected.values()].reduce((sum, file) => sum + file.bytes, 0),
  destination: 'fresh OPFS directory; production worker with actual source Files', status: 'running'};
const writeReceipt = () => fsp.writeFile(path.join(options.out, 'receipt.json'), JSON.stringify(receipt, null, 2) + '\n');
await writeReceipt();
let chrome, socket, server, exportFailure;
const pending = new Map();
const browserErrors = [], networkRequests = [], progress = [];
let sequence = 0;
function send(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = ++sequence;
    const timer = setTimeout(() => {pending.delete(id); reject(Error(`CDP timeout: ${method}`));}, 120000);
    pending.set(id, {resolve, reject, timer});
    socket.send(JSON.stringify({id, method, params}));
  });
}
async function evaluate(expression, awaitPromise = false) {
  const result = await send('Runtime.evaluate', {expression, returnByValue: true, awaitPromise});
  if (result.exceptionDetails) throw Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
  return result.result.value;
}
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
try {
  server = http.createServer(async (request, response) => {
    try {
      if (request.method === 'GET' && request.url === '/prepare.html') {
        response.writeHead(200, {'content-type': 'text/html; charset=utf-8'}); response.end(html); return;
      }
      if (request.method === 'POST' && request.url?.startsWith('/export/')) {
        const relative = decodeURIComponent(request.url.slice('/export/'.length));
        if (!relative || relative.includes('\\') || relative.split('/').some(x => !x || x === '.' || x === '..') || relative.includes('\0')) throw Error('Unsafe export path.');
        const destination = path.join(prepared, relative);
        await fsp.mkdir(path.dirname(destination), {recursive: true});
        await pipeline(request, fs.createWriteStream(destination, {flags: 'wx'}));
        response.writeHead(200); response.end('ok'); return;
      }
      response.writeHead(404); response.end();
    } catch (error) { exportFailure = error; response.writeHead(500); response.end(error.message); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  const profile = path.join(options.out, 'chrome-profile');
  const log = fs.openSync(path.join(options.out, 'chrome.log'), 'w');
  chrome = spawn(chromePath, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--disable-background-networking', '--disable-component-update',
    '--disable-default-apps', '--disable-sync', '--disable-extensions', 'about:blank'], {stdio: ['ignore', log, log]});
  fs.closeSync(log);
  chrome.on('error', error => browserErrors.push(error.message));
  const portFile = path.join(profile, 'DevToolsActivePort');
  for (let i = 0; !fs.existsSync(portFile) && i < 200; ++i) {
    if (chrome.exitCode !== null) throw Error(`Chrome exited: ${chrome.exitCode}. See chrome.log.`);
    await sleep(100);
  }
  const port = (await fsp.readFile(portFile, 'utf8')).split('\n')[0];
  const tabs = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const tab = tabs.find(t => t.type === 'page');
  assert(tab, 'No Chrome page target.');
  socket = new WebSocket(tab.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {socket.onopen = resolve; socket.onerror = reject;});
  socket.onmessage = ({data}) => {
    const message = JSON.parse(data);
    if (message.id) {
      const waiter = pending.get(message.id); if (!waiter) return;
      pending.delete(message.id); clearTimeout(waiter.timer);
      message.error ? waiter.reject(Error(JSON.stringify(message.error))) : waiter.resolve(message.result);
    } else if (message.method === 'Runtime.exceptionThrown') browserErrors.push(message.params.exceptionDetails.exception?.description || message.params.exceptionDetails.text);
    else if (message.method === 'Network.requestWillBeSent') networkRequests.push(message.params.request.url);
  };
  await send('Page.enable'); await send('Runtime.enable'); await send('Network.enable');
  receipt.browser = await send('Browser.getVersion');
  await send('Page.navigate', {url: `${origin}/prepare.html`});
  for (let i = 0; i < 200; ++i) {
    if (await evaluate('typeof globalThis.JGR_PREPARE_WORKER_SOURCE === "string"')) break;
    await sleep(100);
  }
  assert(await evaluate('typeof globalThis.JGR_PREPARE_WORKER_SOURCE === "string"'), 'HTML does not contain the production worker.');
  await evaluate('document.body.insertAdjacentHTML("beforeend", \'<input type="file" multiple id="browser-test-inputs">\')');
  const document = await send('DOM.getDocument');
  const input = await send('DOM.querySelector', {nodeId: document.root.nodeId, selector: '#browser-test-inputs'});
  await send('DOM.setFileInputFiles', {nodeId: input.nodeId, files: inputs});
  const conversionStart = Date.now();
  await evaluate(`(async () => {
    const root = await navigator.storage.getDirectory();
    const name = 'jgr-test-' + crypto.randomUUID();
    const device = await root.getDirectoryHandle(name, {create:true});
    const psp = await device.getDirectoryHandle('PSP', {create:true});
    const games = await psp.getDirectoryHandle('GAME', {create:true});
    const directory = await games.getDirectoryHandle('JGR_PORTABLE', {create:true});
    const sourceFiles = Array.from(document.getElementById('browser-test-inputs').files);
    let saves = null;
    if (${JSON.stringify(options['save-sentinel'] === 'yes')}) {
      const savedata = await psp.getDirectoryHandle('SAVEDATA', {create:true});
      saves = await savedata.getDirectoryHandle('JGR_PORTABLE', {create:true});
      for (const [name, value] of [['jgr_story.sav',0], ['jgr_story.sav.bak',1]]) {
        const file = await saves.getFileHandle(name, {create:true});
        const writer = await file.createWritable();
        await writer.write(new Uint8Array(1024).fill(value)); await writer.close();
      }
    }
    const capture = ${JSON.stringify(options['capture-deleted'] || '')};
    let workerSource = JGR_PREPARE_WORKER_SOURCE;
    if(capture) workerSource += String.fromCharCode(10) + '(' + function(name) {
      const remove = JgrBrowserIO.prototype.remove;
      JgrBrowserIO.prototype.remove = async function(rawPath) {
        const relative = this.path(rawPath);
        if(relative === 'data/assets/' + name) {
          try {
            await this.settlePath(relative);
            const bytes = await (await (await this.handle(relative)).getFile()).arrayBuffer();
            self.postMessage({type:'debug-file',name,bytes},[bytes]);
          } catch(error) {if(this.translate(error).errno !== 2) throw error;}
        }
        return remove.call(this,rawPath);
      };
    }.toString() + ')(' + JSON.stringify(capture) + ');';
    const url = URL.createObjectURL(new Blob([workerSource], {type:'text/javascript'}));
    const worker = new Worker(url);
    window.browserTest = {root,directory,saves,name,worker,url,events:[],debugFiles:[],phase:'starting',current:0,total:0};
    worker.onmessage = ({data}) => {
      if(data.type === 'debug-file') {browserTest.debugFiles.push(data);return;}
      browserTest.events.push(data); if(browserTest.events.length > 40) browserTest.events.shift();
      if(data.type === 'progress') Object.assign(browserTest,{phase:data.phase,current:data.current,total:data.total});
      if(['done','error','cancelled'].includes(data.type)) browserTest.result = data;
    };
    worker.onerror = event => {browserTest.result={type:'error',message:event.message}};
    worker.onmessageerror = () => {browserTest.result={type:'error',message:'Worker message could not be decoded'}};
    worker.postMessage({type:'prepare',directory,sourceFiles});
  })()`, true);
  let final, last = '', lastLog = 0;
  const timeout = Number(options.timeout || 7200) * 1000;
  while (Date.now() - conversionStart < timeout) {
    const state = await evaluate('({phase:browserTest.phase,current:browserTest.current,total:browserTest.total,result:browserTest.result,events:browserTest.events})');
    const stamp = `${state.phase}:${state.current}/${state.total}`;
    if (stamp !== last) {last = stamp; progress.push({elapsed: (Date.now() - conversionStart) / 1000, phase: state.phase, current: state.current, total: state.total});}
    if (Date.now() - lastLog >= 30000 || state.result) {console.log(`${((Date.now()-conversionStart)/1000).toFixed(1)}s ${stamp}${state.result ? ' '+JSON.stringify(state.result) : ''}`); lastLog = Date.now();}
    if (state.result) {final = state.result; receipt.last_events = state.events; break;}
    await sleep(1000);
  }
  receipt.conversion_seconds = (Date.now() - conversionStart) / 1000;
  receipt.worker_result = final;
  if(options['capture-deleted']) {
    receipt.private_debug_injection = options['capture-deleted'];
    receipt.debug_files = await evaluate(`(async()=>{
      const files=[];
      for(const file of browserTest.debugFiles) {
        const response=await fetch('/export/debug/'+encodeURIComponent(file.name),{method:'POST',body:file.bytes});
        if(!response.ok)throw Error(await response.text());
        files.push({name:file.name,bytes:file.bytes.byteLength});
      }
      return files;
    })()`,true);
  }
  if (!final) {await evaluate('browserTest.worker.postMessage({type:"cancel"})'); throw Error('Browser conversion timed out.');}
  assert.equal(final.type, 'done', final.message || 'Browser conversion did not finish.');
  const inventory = await evaluate(`(async () => {
    const files = [], directories = [];
    async function walk(directory,prefix='') {
      for await (const [name,handle] of directory.entries()) {
        const relative=prefix?prefix+'/'+name:name;
        if(handle.kind==='directory') {directories.push(relative);await walk(handle,relative);}
        else {
          const file=await handle.getFile();
          const hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',await file.arrayBuffer())),b=>b.toString(16).padStart(2,'0')).join('');
          files.push({path:relative,bytes:file.size,sha256:hash});
        }
      }
    }
    await walk(browserTest.directory);
    return {files:files.sort((a,b)=>a.path.localeCompare(b.path)),directories:directories.sort()};
  })()`, true);
  const output = inventory.files;
  if (options['save-sentinel'] === 'yes') {
    receipt.synthetic_saves = await evaluate(`(async () => {
      const files = [];
      for await (const [name, handle] of browserTest.saves.entries()) {
        const file = await handle.getFile();
        const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', await file.arrayBuffer())), b => b.toString(16).padStart(2, '0')).join('');
        files.push({path: 'PSP/SAVEDATA/JGR_PORTABLE/' + name, bytes: file.size, sha256: hash});
      }
      return files.sort((a, b) => a.path.localeCompare(b.path));
    })()`, true);
    assert.equal(receipt.synthetic_saves.length, 2, 'Preparation changed the save directory.');
    for (const [name, value] of [['jgr_story.sav',0], ['jgr_story.sav.bak',1]]) {
      const row = receipt.synthetic_saves.find(file => file.path === 'PSP/SAVEDATA/JGR_PORTABLE/' + name);
      assert(row && row.bytes === 1024 && row.sha256 === digest(Buffer.alloc(1024,value)),
        `Preparation changed the existing synthetic save: ${name}`);
    }
    receipt.existing_saves_preserved = true;
  }
  await fsp.writeFile(path.join(options.out, 'outputs.json'), JSON.stringify(output, null, 2) + '\n');
  await fsp.writeFile(path.join(options.out, 'directories.json'), JSON.stringify(inventory.directories, null, 2) + '\n');
  // Empty log folders are part of the installed tree. Preserve them when
  // exporting the test destination for a subsequent ROM-free emulator boot.
  for (const relative of inventory.directories) {
    assert(relative && !/[\\\0]/.test(relative) &&
      relative.split('/').every(part => part && part !== '.' && part !== '..'), 'Unsafe directory export path.');
    await fsp.mkdir(path.join(prepared, relative), {recursive: true});
  }
  receipt.exported_directories = inventory.directories.length;
  const assets = output.filter(f => f.path.startsWith('data/assets/'));
  assert.equal(assets.length, expected.size, 'Output asset count differs.');
  for (const actual of assets) {
    const name = actual.path.slice('data/assets/'.length);
    const wanted = expected.get(name);
    assert(wanted, `Unexpected asset: ${name}`);
    assert.equal(actual.bytes, wanted.bytes, `${name}: size differs`);
    assert.equal(actual.sha256, wanted.sha256, `${name}: checksum differs`);
  }
  assert(output.some(f => f.path === 'data/cache/assets.checked'), 'Completion marker missing.');
  receipt.exact_assets = assets.length;
  receipt.exact_asset_bytes = assets.reduce((sum, f) => sum + f.bytes, 0);
  await evaluate(`(async () => {
    async function walk(directory,prefix='') {
      for await (const [name,handle] of directory.entries()) {
        const relative=prefix?prefix+'/'+name:name;
        if(handle.kind==='directory') await walk(handle,relative);
        else {const response=await fetch('/export/'+encodeURIComponent(relative),{method:'POST',body:await handle.getFile()});if(!response.ok)throw Error(await response.text());}
      }
    }
    await walk(browserTest.directory);
  })()`, true);
  if (exportFailure) throw exportFailure;
  for (const file of output) {
    const exported = path.join(prepared, file.path);
    assert.equal((await fsp.stat(exported)).size, file.bytes, `Export size differs: ${file.path}`);
    assert.equal(await fileHash(exported), file.sha256, `Export checksum differs: ${file.path}`);
  }
  await fsp.copyFile(options.eboot, path.join(prepared, 'EBOOT.PBP'));
  await evaluate('(async()=>{browserTest.worker.terminate();URL.revokeObjectURL(browserTest.url);await browserTest.root.removeEntry(browserTest.name,{recursive:true});})()', true);
  const foreignRequests = networkRequests.filter(url => !url.startsWith(origin + '/') && !url.startsWith('blob:' + origin + '/'));
  assert.deepEqual(foreignRequests, [], 'Preparation made an external network request.');
  assert.deepEqual(browserErrors, [], 'Uncaught browser errors.');
  receipt.status = 'passed'; receipt.export_directory = prepared;
  console.log(`PASS: ${assets.length} assets/manifest exact; ${receipt.conversion_seconds.toFixed(1)}s conversion; exported ${output.length} files.`);
} catch (error) {
  receipt.status = 'failed'; receipt.error = error.stack || String(error); process.exitCode = 1;
  console.error(receipt.error);
} finally {
  receipt.finished = new Date().toISOString(); receipt.browser_errors = browserErrors;
  await fsp.writeFile(path.join(options.out, 'progress.json'), JSON.stringify(progress, null, 2) + '\n');
  await fsp.writeFile(path.join(options.out, 'network.json'), JSON.stringify(networkRequests, null, 2) + '\n');
  await writeReceipt();
  for (const waiter of pending.values()) {clearTimeout(waiter.timer); waiter.reject(Error('Browser test ended.'));}
  pending.clear(); socket?.close(); chrome?.kill('SIGTERM'); server?.close();
}
