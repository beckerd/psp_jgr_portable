/* The release builder embeds filesystem.js and the single-file Emscripten
 * module before this worker. No network request is needed to prepare assets. */
let running = false;
let cancelled = false;
self.onmessage = async ({data}) => {
  if (data?.type === 'cancel') { cancelled = true; return; }
  if (data?.type !== 'prepare' || running) return;
  running = true;
  cancelled = false;
  let io;
  const started = performance.now();
  let lastProgress = 0;
  let lastPhase = '';
  const progress = message => {
    const now = performance.now();
    if (message.phase !== lastPhase || now - lastProgress >= 100 || message.current === message.total) {
      lastProgress = now; lastPhase = message.phase;
      self.postMessage({type: 'progress', ...message});
    }
  };
  try {
    if (!data.directory || data.directory.kind !== 'directory') throw new Error('Choose the game folder before preparing assets.');
    // sourceFiles is also useful for an explicit file-selection frontend; the
    // normal folder UI supplies none. Files are read-only and remain on disk.
    io = new JgrBrowserIO(data.directory, data.sourceFiles || []);
    const entries = await io.list('/game');
    const gdis = entries.filter(entry => /\.gdi$/i.test(entry.name) && (entry.mode & 0xf000) === 0x8000);
    if (gdis.length !== 1) throw new Error('The selected folder must contain exactly one GDI file and its track files.');
    if (!entries.some(entry => entry.name === 'EBOOT.PBP' && (entry.mode & 0xf000) === 0x8000)) {
      throw new Error('Place this release’s EBOOT.PBP in the selected folder.');
    }
    progress({phase: 'bundle', current: 0, total: 0});
    await jgrPrepareBackground(io);
    if (cancelled) { self.postMessage({type: 'cancelled'}); return; }
    const module = await createJgrPrepareModule({
      browserIo: io,
      // Browsers cannot report free space on a user-selected external volume.
      // The adapter checks every actual write and close instead.
      prepareAvailableBytes: null,
      prepareCancelled: () => cancelled,
      prepareProgress: progress,
      print: () => {},
      printErr: text => console.error(text),
    });
    if (cancelled) { self.postMessage({type: 'cancelled'}); return; }
    const result = await module.ccall('jgr_browser_prepare', 'number', ['string', 'string'],
      ['/game', '/game/'+gdis[0].name], {async: true});
    await io.closeAll();
    if (result === -7) self.postMessage({type: 'cancelled'});
    else if (result !== 0) {
      throw new Error(module.UTF8ToString(module._jgr_browser_error()) || 'Asset preparation failed.');
    } else {
      self.postMessage({type: 'done', assets: module._jgr_browser_asset_count(),
        elapsedSeconds: (performance.now()-started)/1000,
        bytesRead: io.bytesRead, bytesWritten: io.bytesWritten});
    }
  } catch (error) {
    if (io) try { await io.closeAll(); } catch (closeError) { console.error(closeError); }
    self.postMessage({type: 'error', message: error?.message || String(error),
      detail: error?.stack || ''});
  } finally { running = false; }
};
