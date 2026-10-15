/* Async File System Access adapter. Paths are confined to the selected game
 * directory; only generated data subdirectories can be modified. Source files
 * remain local File objects and are never loaded wholesale into Wasm memory. */
globalThis.JgrBrowserIO = class JgrBrowserIO {
  constructor(directory, sourceFiles = []) {
    this.directory = directory;
    this.sources = new Map();
    for (const file of sourceFiles) {
      if (!file.name || file.name.includes('/') || file.name.includes('\\') || this.sources.has(file.name)) {
        throw this.error(22, 'Invalid or duplicate source filename.');
      }
      this.sources.set(file.name, file);
    }
    this.files = new Map();
    this.versions = new Map();
    this.nextFd = 1000;
    this.reader = typeof FileReaderSync === 'function' ? new FileReaderSync() : null;
    this.directoryCache = new Map([['', directory]]);
    this.bytesRead = 0;
    this.bytesWritten = 0;
  }
  error(errno, message) { return Object.assign(new Error(message), {errno}); }
  translate(error) {
    this.lastError = `${error.name || 'Error'}: ${error.message || 'Could not access the selected folder.'}`;
    if (error.errno) return error;
    const code = {NotFoundError: 2, NotAllowedError: 13, SecurityError: 13,
      TypeMismatchError: 20, InvalidModificationError: 39, QuotaExceededError: 28,
      NoModificationAllowedError: 13, NotSupportedError: 38, AbortError: 5}[error.name] || 5;
    return this.error(code, error.message || 'Could not access the selected folder.');
  }
  path(path) {
    if (typeof path !== 'string' || path.includes('\0') || path.includes('\\')) throw this.error(22, 'Invalid path.');
    if (path === '/' || path === '/game' || path === '/game/') return '';
    if (!path.startsWith('/game/')) throw this.error(13, 'Path is outside the selected folder.');
    const parts = path.slice(6).split('/').filter(part => part && part !== '.');
    if (parts.some(part => part === '..')) throw this.error(13, 'Parent paths are not allowed.');
    return parts.join('/');
  }
  writable(path, directory = false) {
    if (directory && ['data', 'data/log'].includes(path)) return;
    if (!/^data\/(assets|cache|setup)(\/|$)/.test(path)) throw this.error(13, 'Only generated asset folders may be changed.');
  }
  async dir(path) {
    if (this.directoryCache.has(path)) return this.directoryCache.get(path);
    const parts = path.split('/');
    let handle = this.directory;
    let prefix = '';
    for (const name of parts) {
      prefix += (prefix ? '/' : '') + name;
      if (this.directoryCache.has(prefix)) handle = this.directoryCache.get(prefix);
      else {
        handle = await handle.getDirectoryHandle(name);
        this.directoryCache.set(prefix, handle);
      }
    }
    return handle;
  }
  async parent(path) {
    const slash = path.lastIndexOf('/');
    return {parent: await this.dir(slash < 0 ? '' : path.slice(0, slash)), name: path.slice(slash + 1)};
  }
  async handle(path, create = false) {
    const {parent, name} = await this.parent(path);
    return parent.getFileHandle(name, {create});
  }
  async commit(entry) {
    if (entry.writer) {
      const writer = entry.writer;
      entry.writer = null;
      try { await writer.close(); }
      catch (error) { entry.failed = true; throw error; }
      entry.file = await entry.handle.getFile();
      entry.size = entry.file.size;
      entry.fileVersion = (this.versions.get(entry.path) || 0) + 1;
      this.versions.set(entry.path, entry.fileVersion);
    }
  }
  async settlePath(path, except = null) {
    for (const entry of this.files.values()) if (entry.path === path && entry !== except) await this.commit(entry);
  }
  async refresh(entry) {
    const writer = [...this.files.values()].find(other => other.path === entry.path && other.writer);
    if (writer) entry.size = writer.size;
    else if (entry.handle && entry.fileVersion !== (this.versions.get(entry.path) || 0)) {
      entry.file = await entry.handle.getFile();
      entry.fileVersion = this.versions.get(entry.path) || 0;
      entry.size = entry.file.size;
    }
  }
  async open(rawPath, mode) {
    const path = this.path(rawPath);
    if (!path) throw this.error(21, 'Cannot open a directory as a file.');
    const writing = /[wa+]/.test(mode), reading = mode.startsWith('r') || mode.includes('+');
    if (writing) this.writable(path);
    await this.settlePath(path);
    let handle = null, file;
    if (this.sources.has(path)) {
      if (writing) throw this.error(13, 'Source disc files are read-only.');
      file = this.sources.get(path);
    } else {
      handle = await this.handle(path, /^[wa]/.test(mode));
      file = await handle.getFile();
    }
    const entry = {path, handle, file, reading, writing, size: file.size,
      position: mode.startsWith('a') ? file.size : 0, append: mode.startsWith('a'), writer: null, failed: false,
      fileVersion: this.versions.get(path) || 0};
    if (mode.startsWith('w')) {
      entry.writer = await handle.createWritable({keepExistingData: false});
      entry.size = 0;
    }
    const fd = this.nextFd++;
    this.files.set(fd, entry);
    return fd;
  }
  entry(fd) {
    const entry = this.files.get(fd);
    if (!entry) throw this.error(9, 'File is closed.');
    if (entry.failed) throw this.error(5, 'An earlier write failed.');
    return entry;
  }
  async read(fd, count) {
    const entry = this.entry(fd);
    if (!entry.reading) throw this.error(9, 'File is not readable.');
    await this.settlePath(entry.path);
    await this.refresh(entry);
    const blob = entry.file.slice(entry.position, entry.position + count);
    const bytes = new Uint8Array(this.reader ? this.reader.readAsArrayBuffer(blob) : await blob.arrayBuffer());
    entry.position += bytes.length;
    this.bytesRead += bytes.length;
    return bytes;
  }
  async write(fd, bytes) {
    const entry = this.entry(fd);
    if (!entry.writing) throw this.error(9, 'File is not writable.');
    await this.settlePath(entry.path, entry);
    await this.refresh(entry);
    if (!entry.writer) entry.writer = await entry.handle.createWritable({keepExistingData: true});
    if (entry.append) entry.position = entry.size;
    try { await entry.writer.write({type: 'write', position: entry.position, data: bytes}); }
    catch (error) { entry.failed = true; throw error; }
    entry.position += bytes.byteLength;
    entry.size = Math.max(entry.size, entry.position);
    this.bytesWritten += bytes.byteLength;
    return bytes.byteLength;
  }
  async seek(fd, offset, whence) {
    const entry = this.entry(fd);
    await this.refresh(entry);
    const position = (whence === 0 ? 0 : whence === 1 ? entry.position : whence === 2 ? entry.size : NaN) + offset;
    if (!Number.isSafeInteger(position) || position < 0) throw this.error(22, 'Invalid seek.');
    entry.position = position;
    return position;
  }
  async close(fd) {
    const entry = this.files.get(fd);
    if (!entry) throw this.error(9, 'File is closed.');
    try {
      if (entry.failed) {
        if (entry.writer) await entry.writer.abort();
        throw this.error(5, 'File write failed.');
      }
      await this.commit(entry);
    } finally { this.files.delete(fd); }
    return 0;
  }
  async statFd(fd) {
    const entry = this.entry(fd);
    await this.refresh(entry);
    return {size: entry.size, mode: 0x8000 | 0o666, mtime: (entry.file.lastModified || 0) / 1000};
  }
  async stat(rawPath) {
    const path = this.path(rawPath);
    if (!path || this.directoryCache.has(path)) return {size: 0, mode: 0x4000 | 0o777, mtime: 0};
    if (this.sources.has(path)) {
      const file = this.sources.get(path);
      return {size: file.size, mode: 0x8000 | 0o444, mtime: file.lastModified / 1000};
    }
    for (const [fd, entry] of this.files) if (entry.path === path) return this.statFd(fd);
    const {parent, name} = await this.parent(path);
    try {
      const file = await (await parent.getFileHandle(name)).getFile();
      return {size: file.size, mode: 0x8000 | 0o666, mtime: file.lastModified / 1000};
    } catch (error) {
      if (error.name !== 'TypeMismatchError') throw error;
      this.directoryCache.set(path, await parent.getDirectoryHandle(name));
      return {size: 0, mode: 0x4000 | 0o777, mtime: 0};
    }
  }
  async mkdir(rawPath) {
    const path = this.path(rawPath);
    this.writable(path, true);
    try { await this.stat(rawPath); }
    catch (error) {
      if (this.translate(error).errno !== 2) throw error;
      const {parent, name} = await this.parent(path);
      this.directoryCache.set(path, await parent.getDirectoryHandle(name, {create: true}));
      return 0;
    }
    throw this.error(17, 'Directory already exists.');
  }
  async remove(rawPath) {
    const path = this.path(rawPath);
    this.writable(path);
    await this.settlePath(path);
    if ([...this.files.values()].some(entry => entry.path === path)) throw this.error(13, 'Cannot remove an open file.');
    const {parent, name} = await this.parent(path);
    await parent.removeEntry(name);
    this.directoryCache.delete(path);
    return 0;
  }
  async rename(rawFrom, rawTo) {
    const from = this.path(rawFrom), to = this.path(rawTo);
    this.writable(from); this.writable(to);
    await this.settlePath(from); await this.settlePath(to);
    if (from === to) return 0;
    if ([...this.files.values()].some(entry => entry.path === from || entry.path === to)) throw this.error(13, 'Cannot publish an open file.');
    const source = await this.handle(from);
    const sourceParent = await this.parent(from), targetParent = await this.parent(to);
    if (typeof source.move === 'function') {
      try { await source.move(targetParent.parent, targetParent.name); return 0; }
      catch (error) {
        if (!['NotSupportedError', 'NotAllowedError', 'InvalidModificationError'].includes(error.name)) throw error;
      }
    }
    // External-directory move is not supported in every Chromium version.
    // Stream to the browser's transactional writable, commit, then remove
    // the old temporary. An interrupted publish never creates a ready marker.
    const file = await source.getFile();
    const target = await targetParent.parent.getFileHandle(targetParent.name, {create: true});
    const writer = await target.createWritable();
    await file.stream().pipeTo(writer);
    if ((await target.getFile()).size !== file.size) throw this.error(5, 'Published file size differs.');
    await sourceParent.parent.removeEntry(sourceParent.name);
    return 0;
  }
  async list(rawPath) {
    const path = this.path(rawPath), directory = await this.dir(path);
    const entries = new Map();
    for await (const [name, handle] of directory.entries()) entries.set(name, {name, mode: handle.kind === 'directory' ? 0x4000 | 0o777 : 0x8000 | 0o666});
    if (!path) for (const name of this.sources.keys()) entries.set(name, {name, mode: 0x8000 | 0o444});
    return [...entries.values()].sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
  }
  async closeAll() {
    let first;
    for (const fd of [...this.files.keys()]) try { await this.close(fd); } catch (error) { first ||= error; }
    if (first) throw first;
  }
};
