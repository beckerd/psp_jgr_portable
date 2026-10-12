/* Offline preparer UI. The release builder embeds this file after defining
 * JGR_PREPARE_WORKER_SOURCE. It never executes native programs or fetches URLs.
 * Worker protocol: prepare(directory), cancel; progress(phase,current,total,
 * completedBytes,requiredBytes), done, error(message), cancelled. */
(() => {
  'use strict';
  const element = id => document.getElementById(id);
  const ui = Object.fromEntries([
    'compatibility', 'choose-folder', 'folder-name', 'folder-summary',
    'folder-message', 'extract', 'cancel', 'progress-card', 'phase-label',
    'phase-description', 'progress-value', 'progress-track', 'progress-bar',
    'progress-count', 'elapsed', 'keep-open', 'success-card',
    'status-announcement', 'error-details', 'error-text',
    'chrome-required', 'launch-chrome', 'chrome-instructions', 'folder-card', 'intro',
    'chrome-address', 'page-address', 'copy-address', 'copy-status',
    'source-check', 'source-check-message', 'source-file-list',
  ].map(id => [id, element(id)]));
  const phases = {
    bundle: ['Opening preparation tools', 'Getting everything ready on this computer.'],
    plan: ['Checking preparation tools', 'Checking the instructions for this release.'],
    cleanup: ['Getting ready', 'Preparing the working space.'],
    scan: ['Checking installed assets', 'Looking for assets already prepared for this release.'],
    space: ['Preparing the output folder', 'Getting the destination ready for the prepared assets.'],
    disc: ['Checking your game image', 'Confirming that the game image is supported and complete.'],
    convert: ['Preparing game assets', 'Converting game, graphics, and audio data for the PSP.'],
    verify: ['Verifying prepared assets', 'Checking the finished assets before marking the folder ready.'],
    publish: ['Finishing up', 'Saving the completed installation.'],
    done: ['Game assets are ready', 'The prepared folder is ready to copy to your PSP.'],
  };
  const numericPhases = ['plan', 'cleanup', 'scan', 'space', 'disc', 'convert', 'verify', 'publish', 'done'];
  let directory = null;
  let worker = null;
  let workerURL = null;
  let active = false;
  let selecting = false;
  let cancelling = false;
  let started = 0;
  let timer = null;
  let lastPhase = '';
  let compatible = true;
  let folderChoiceVersion = 0;

  // Only write the clipboard on an explicit copy/launch click. Keep the native
  // launch link synchronous so the browser retains its user activation.
  const pageAddress = new URL(window.location.href);
  pageAddress.hash = '';
  ui['page-address'].value = pageAddress.href;
  async function copyPageAddress() {
    ui['copy-status'].textContent = 'Copying page address…';
    try {
      await navigator.clipboard.writeText(pageAddress.href);
    } catch (_) {
      const previousFocus = document.activeElement;
      ui['page-address'].focus({ preventScroll: true });
      ui['page-address'].select();
      let copied = false;
      try { copied = document.execCommand('copy'); } catch (_) {}
      if (!copied) {
        ui['copy-status'].textContent = 'Automatic copying was blocked. The address is selected; copy it, then paste it into Chrome’s address bar.';
        return;
      }
      previousFocus?.focus({ preventScroll: true });
    }
    ui['copy-status'].textContent = 'Page address copied. Paste it into Chrome’s address bar and press Enter.';
  }
  ui['copy-address'].addEventListener('click', copyPageAddress);
  ui['launch-chrome'].addEventListener('click', () => { void copyPageAddress(); });

  // A path from location.href cannot authorize a folder. Remember only a
  // handle the user selected, separately for each preparer's directory.
  function savedFolder(value) {
    return new Promise(resolve => {
      let database, finished = false;
      const finish = result => {
        if (finished) return;
        finished = true;
        clearTimeout(timeout);
        if (database) database.close();
        resolve(result);
      };
      const timeout = setTimeout(() => finish(null), 2000);
      try {
        const key = new URL('.', window.location.href).href;
        const request = indexedDB.open('jgr-portable-preparer', 1);
        request.onupgradeneeded = () => request.result.createObjectStore('folders');
        request.onerror = request.onblocked = () => finish(null);
        request.onsuccess = () => {
          database = request.result;
          if (finished) { database.close(); return; }
          try {
            const transaction = database.transaction('folders', value ? 'readwrite' : 'readonly');
            const store = transaction.objectStore('folders');
            const operation = value ? store.put(value, key) : store.get(key);
            let result = null;
            operation.onsuccess = () => { result = operation.result; };
            transaction.oncomplete = () => finish(value ? true : result);
            transaction.onerror = transaction.onabort = () => finish(null);
          } catch (_) { finish(null); }
        };
      } catch (_) { finish(null); }
    });
  }

  async function restoreFolder() {
    const choiceVersion = folderChoiceVersion;
    const previous = await savedFolder();
    // Never replace a folder the user is choosing while IndexedDB loads.
    if (choiceVersion !== folderChoiceVersion || directory || active || selecting ||
        previous?.kind !== 'directory' || typeof previous.requestPermission !== 'function') return;
    directory = previous;
    ui['folder-name'].textContent = previous.name;
    ui['folder-summary'].textContent = 'Last used folder restored. Chrome may ask for access when you prepare.';
    ui['choose-folder'].innerHTML = 'Change folder <span aria-hidden="true">↗</span>';
    setState('idle', 'Folder selected', 'Click Prepare Assets to allow access and check your game files.');
    controls();
  }

  function announce(message) {
    ui['status-announcement'].textContent = message;
  }

  function setState(state, title, description) {
    ui['progress-card'].dataset.state = state;
    ui['phase-label'].textContent = title;
    ui['phase-description'].textContent = description;
    announce(title);
  }

  function elapsed() {
    const seconds = started ? Math.max(0, Math.floor((performance.now() - started) / 1000)) : 0;
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor(seconds / 60) % 60;
    const clock = `${String(minutes).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`;
    ui.elapsed.querySelector('strong').textContent = hours ? `${hours}:${clock}` : clock;
  }

  function controls() {
    ui['choose-folder'].disabled = active || selecting || !compatible;
    ui.extract.disabled = active || selecting || !directory || !compatible;
    ui.cancel.hidden = !active;
    ui.cancel.disabled = cancelling;
    ui.cancel.textContent = cancelling ? 'Stopping…' : 'Cancel';
    ui['keep-open'].textContent = active
      ? 'Keep this page open until preparation finishes.'
      : '';
    ui['keep-open'].hidden = !active;
  }

  function stopWorker() {
    if (worker) worker.terminate();
    worker = null;
    if (workerURL) URL.revokeObjectURL(workerURL);
    workerURL = null;
  }

  function finish() {
    active = false;
    cancelling = false;
    elapsed();
    clearInterval(timer);
    timer = null;
    stopWorker();
    ui['progress-track'].classList.remove('indeterminate');
    controls();
  }

  class SourceFilesError extends Error {
    constructor(message, files) {
      super(message);
      this.files = files;
    }
  }

  function showSourceFiles(files, message = 'All required files were found.') {
    const rows = files.map(file => {
      const row = document.createElement('li');
      row.dataset.found = String(file.found);
      const icon = document.createElement('span');
      icon.className = 'source-file-icon';
      icon.setAttribute('aria-hidden', 'true');
      icon.textContent = file.found ? '✓' : '×';
      const name = document.createElement('span');
      name.className = 'source-file-name';
      const status = document.createElement('span');
      status.className = 'visually-hidden';
      status.textContent = file.found ? 'Found: ' : 'Missing: ';
      name.append(status, document.createTextNode(file.name));
      row.append(icon, name);
      return row;
    });
    ui['source-file-list'].replaceChildren(...rows);
    ui['source-check-message'].textContent = message;
    ui['source-check'].hidden = false;
  }

  function error(failure) {
    finish();
    if (failure instanceof SourceFilesError) {
      showSourceFiles(failure.files, failure.message);
      setState('error', 'Check your game files', 'Review the required files below, then try again.');
      ui['error-details'].hidden = true;
      ui['folder-summary'].textContent = 'The required files need attention. Review the checklist below.';
    } else {
      const message = typeof failure === 'string' ? failure : failure?.message;
      setState('error', 'Preparation could not finish', 'Review the details below, then try again. Keep the original game image and track files together.');
      ui['error-text'].textContent = message || 'An unexpected preparation error occurred.';
      ui['error-details'].hidden = false;
      ui['error-details'].open = true;
    }
    ui.extract.innerHTML = 'Try again <span aria-hidden="true">→</span>';
    ui['success-card'].hidden = true;
  }

  function cancelled() {
    finish();
    setState('cancelled', 'Preparation cancelled', 'You can start preparation again when you’re ready.');
    ui.extract.innerHTML = 'Prepare Assets <span aria-hidden="true">→</span>';
  }

  function progress(message) {
    if (cancelling) return;
    const phase = typeof message.phase === 'number' ? numericPhases[message.phase] : message.phase;
    const description = phases[phase] || phases.convert;
    if (phase !== lastPhase) {
      lastPhase = phase;
      setState('preparing', description[0], description[1]);
    }
    const current = Number(message.current);
    const total = Number(message.total);
    const determinate = Number.isFinite(current) && Number.isFinite(total) && current >= 0 && total > 0;
    ui['progress-track'].classList.toggle('indeterminate', !determinate);
    if (determinate) {
      const percentage = Math.min(100, Math.max(0, current / total * 100));
      ui['progress-value'].textContent = `${Math.floor(percentage)}%`;
      ui['progress-bar'].style.width = `${percentage}%`;
      ui['progress-bar'].setAttribute('aria-valuenow', String(Math.floor(percentage)));
      ui['progress-bar'].setAttribute('aria-label', `${description[0]} progress`);
      ui['progress-count'].textContent = phase === 'convert' || phase === 'verify' || phase === 'scan'
        ? `${Math.min(current, total).toLocaleString()} of ${total.toLocaleString()} items`
        : 'Current step';
    } else {
      ui['progress-value'].textContent = '…';
      ui['progress-bar'].removeAttribute('aria-valuenow');
      ui['progress-count'].textContent = 'Working…';
    }
    // Raw filenames and converter details intentionally stay out of progress.
  }

  function done() {
    finish();
    setState('done', phases.done[0], phases.done[1]);
    ui['progress-value'].textContent = '100%';
    ui['progress-bar'].style.width = '100%';
    ui['progress-bar'].setAttribute('aria-valuenow', '100');
    ui['progress-bar'].setAttribute('aria-label', 'Asset preparation complete');
    ui['progress-count'].textContent = 'Preparation complete';
    ui.extract.innerHTML = 'Check assets again <span aria-hidden="true">→</span>';
    ui['success-card'].hidden = false;
    ui['success-card'].scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  }

  async function checkSourceFiles(selected) {
    const files = new Map();
    for await (const entry of selected.values()) if (entry.kind === 'file') files.set(entry.name, entry);
    const images = [...files.keys()].filter(name => name.toLowerCase().endsWith('.gdi'));
    const required = [{ name: 'EBOOT.PBP', found: files.has('EBOOT.PBP') }];
    for (const name of images) required.push({ name, found: true });
    if (!images.length) required.push({ name: 'GDI game image (.gdi)', found: false });
    if (images.length > 1) throw new SourceFilesError('This folder contains more than one GDI file. Keep one game image with its track files in the release folder.', required);
    if (images.length === 1) {
      let text;
      try {
        const image = await files.get(images[0]).getFile();
        if (image.size > 65536) throw new SourceFilesError('The GDI file is not a supported track listing. Use the original GDI with all its track files.', required);
        text = await image.text();
      } catch (failure) {
        if (failure instanceof SourceFilesError) throw failure;
        throw new SourceFilesError('The GDI file could not be read. Check access to the file, then choose Try again to check its track list.', required);
      }
      const lines = text.trim().split(/\r?\n/);
      const count = Number(lines.shift());
      if (!Number.isInteger(count) || count < 1 || count > 99 || count !== lines.length) {
        throw new SourceFilesError('The GDI track listing is incomplete or invalid. Use the original GDI with all its track files.', required);
      }
      const tracks = new Set();
      for (const line of lines) {
        const match = line.match(/^\s*\d+\s+\d+\s+\d+\s+\d+\s+(?:"([^"\r\n]+)"|(\S+))\s+\d+\s*$/);
        const name = match && (match[1] || match[2]);
        if (!name || name === '.' || name === '..' || /[\\/\0]/.test(name)) {
          throw new SourceFilesError('Keep the original GDI and its track files together in this folder, with their original filenames.', required);
        }
        tracks.add(name);
      }
      for (const name of tracks) required.push({ name, found: files.has(name) });
    }
    if (required.some(file => !file.found)) {
      throw new SourceFilesError(images.length
        ? 'Add the missing files to this folder, then choose Try again.'
        : 'Add the missing files, then choose Try again. The GDI is needed to list and check its track files.', required);
    }
    return required;
  }

  ui['choose-folder'].addEventListener('click', async () => {
    if (active || selecting || !compatible) return;
    folderChoiceVersion++;
    selecting = true;
    controls();
    ui['folder-message'].hidden = true;
    let selected;
    try {
      selected = await window.showDirectoryPicker({ mode: 'readwrite', id: 'jgr-portable-release' });
      directory = selected;
      ui['folder-name'].textContent = selected.name;
      ui['folder-summary'].textContent = 'Checking required files…';
      ui['choose-folder'].innerHTML = 'Change folder <span aria-hidden="true">↗</span>';
      ui['success-card'].hidden = true;
      ui['error-details'].hidden = true;
      ui['source-check'].hidden = true;
      ui['source-file-list'].replaceChildren();
      ui['progress-value'].textContent = '—';
      ui['progress-bar'].style.width = '0%';
      ui['progress-bar'].setAttribute('aria-valuenow', '0');
      ui['progress-count'].textContent = 'One-time preparation';
      started = 0;
      elapsed();
      showSourceFiles(await checkSourceFiles(selected));
      void savedFolder(selected);
      ui['folder-summary'].textContent = 'Game executable, GDI, and all listed track files found.';
      setState('idle', 'Ready to prepare', 'Your game image will be checked before extraction begins.');
      ui.extract.innerHTML = 'Prepare Assets <span aria-hidden="true">→</span>';
      announce('Folder selected. Ready to prepare game assets.');
    } catch (failure) {
      if (!selected && failure && failure.name === 'AbortError') return;
      if (selected) {
        if (!(failure instanceof SourceFilesError)) {
          ui['folder-summary'].textContent = 'The folder could not be checked. Try again or choose another folder.';
        }
        error(failure);
        return;
      }
      directory = null;
      ui['source-check'].hidden = true;
      ui['source-file-list'].replaceChildren();
      ui['success-card'].hidden = true;
      ui['error-details'].hidden = true;
      setState('idle', 'Choose a folder to continue', 'Select the release folder containing your game image.');
      ui['progress-value'].textContent = '—';
      ui['progress-bar'].style.width = '0%';
      ui['progress-bar'].setAttribute('aria-valuenow', '0');
      ui['folder-name'].textContent = 'Choose your release folder';
      ui['folder-summary'].textContent = 'The game executable and game image should be in the same folder.';
      ui['folder-message'].textContent = failure && failure.name === 'NotAllowedError'
        ? 'Allow read and write access to the release folder, then try again.'
        : failure && failure.message || 'The folder could not be opened. Please try again.';
      ui['folder-message'].hidden = false;
    } finally {
      selecting = false;
      controls();
    }
  });

  ui.extract.addEventListener('click', async () => {
    if (!directory || active || !compatible) return;
    active = true;
    cancelling = false;
    controls();
    ui['error-details'].hidden = true;
    ui['error-details'].open = false;
    ui['success-card'].hidden = true;
    let sourceChecked = false;
    try {
      // Called directly from the click handler so a browser may ask permission.
      if (await directory.requestPermission({ mode: 'readwrite' }) !== 'granted') {
        throw new Error('Read and write access was not granted. Choose the release folder again and allow access.');
      }
      // A remembered folder may have moved or changed since the last visit.
      // Check it only after Chrome grants access, before starting the worker.
      showSourceFiles(await checkSourceFiles(directory));
      sourceChecked = true;
      void savedFolder(directory);
      ui['folder-summary'].textContent = 'Game executable, GDI, and all listed track files found.';
      if (cancelling) {
        cancelled();
        return;
      }
      started = performance.now();
      elapsed();
      timer = setInterval(elapsed, 1000);
      lastPhase = '';
      progress({ phase: 'bundle' });
      workerURL = URL.createObjectURL(new Blob([globalThis.JGR_PREPARE_WORKER_SOURCE], { type: 'text/javascript' }));
      worker = new Worker(workerURL);
      worker.onmessage = ({ data }) => {
        if (!active || !data || typeof data !== 'object') return;
        if (data.type === 'progress') progress(data);
        else if (data.type === 'done') done();
        else if (data.type === 'cancelled') cancelled();
        else if (data.type === 'error') error(data.message);
      };
      worker.onerror = event => {
        event.preventDefault();
        error(event.message || 'The preparation worker stopped unexpectedly.');
      };
      worker.onmessageerror = () => error('The browser could not communicate with the preparation worker.');
      worker.postMessage({ type: 'prepare', directory });
    } catch (failure) {
      if (!sourceChecked && !(failure instanceof SourceFilesError) && !ui['source-check'].hidden) {
        ui['source-check-message'].textContent = 'These results are from the previous file check. This attempt could not recheck the folder.';
      }
      if (cancelling) {
        if (failure instanceof SourceFilesError) showSourceFiles(failure.files, failure.message);
        cancelled();
      } else {
        error(failure);
      }
    }
  });

  ui.cancel.addEventListener('click', () => {
    if (!active || cancelling) return;
    cancelling = true;
    setState('cancelling', 'Stopping preparation', 'Finishing the current operation safely. Keep this page open.');
    controls();
    if (worker) worker.postMessage({ type: 'cancel' });
  });

  window.addEventListener('beforeunload', event => {
    if (!active) return;
    event.preventDefault();
    event.returnValue = '';
  });

  // This is a support requirement, not a security boundary. Prefer Chrome's
  // brand metadata; other Chromium browsers share its legacy user-agent token.
  const userAgent = navigator.userAgent || '';
  const brands = navigator.userAgentData?.brands;
  const mobile = navigator.userAgentData?.mobile === true ||
    /Android|iPhone|iPad|iPod|Mobile/i.test(userAgent) ||
    (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
  const otherBrowser = /Edg(?:e|A|iOS)?\/|OPR\/|Opera|Vivaldi\/|YaBrowser\/|SamsungBrowser\/|Electron\//i.test(userAgent) ||
    !!navigator.brave;
  const chrome = !mobile && !otherBrowser &&
    (brands?.length ? brands.some(entry => entry.brand === 'Google Chrome') :
      /(?:^|\s)(?:Headless)?Chrome\/\d+/.test(userAgent) && navigator.vendor === 'Google Inc.');
  ui.intro.hidden = !chrome;
  let problem = '';
  if (!chrome) {
    compatible = false;
    ui['chrome-required'].hidden = false;
    ui['folder-card'].hidden = true;
    ui['progress-card'].hidden = true;
    if (mobile) {
      ui['launch-chrome'].hidden = true;
      ui['chrome-address'].hidden = true;
      ui['chrome-instructions'].textContent = 'Move the release folder to a Windows, Mac, or Linux computer, then open the preparer HTML file in Google Chrome there.';
    }
    announce('Google Chrome on a computer is required to prepare game assets.');
  } else if (!window.isSecureContext || typeof window.showDirectoryPicker !== 'function' || typeof Worker !== 'function') {
    problem = 'This copy of Google Chrome cannot access the required folder tools. Update Chrome, then reopen the preparer HTML file on your computer.';
  } else if (typeof globalThis.JGR_PREPARE_WORKER_SOURCE !== 'string' || !globalThis.JGR_PREPARE_WORKER_SOURCE.length) {
    problem = 'This is the source template. Download the versioned preparer HTML file from a release to prepare your game assets.';
  }
  if (problem) {
    compatible = false;
    ui.compatibility.textContent = problem;
    ui.compatibility.hidden = false;
    setState('unavailable', 'Preparation is unavailable', 'See the message above for the next step.');
  }
  controls();
  if (compatible) void restoreFolder();
})();
