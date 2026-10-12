/* Optional PSP startup artwork comes only from the selected EBOOT's PIC1.
 * No image is embedded in this preparer or added to the verified game pack. */
globalThis.jgrPrepareBackground = async function (io) {
  const width = 480, height = 272, maximumPng = 2 * 1024 * 1024;
  const invalid = () => new Error('EBOOT.PBP contains invalid startup artwork.');
  let file = await io.open('/game/EBOOT.PBP', 'rb'), png;
  try {
    const size = (await io.statFd(file)).size;
    const header = await io.read(file, 40);
    if (header.length !== 40 || header[0] || header[1] !== 80 || header[2] !== 66 || header[3] !== 80) throw invalid();
    const words = new DataView(header.buffer, header.byteOffset, header.byteLength);
    const offsets = Array.from({length: 8}, (_, i) => words.getUint32(8 + i * 4, true));
    if (offsets[0] !== 40 || offsets.some((offset, i) => offset > size || (i && offset < offsets[i - 1]))) throw invalid();
    const length = offsets[5] - offsets[4];
    if (!length) return false;
    if (length < 33 || length > maximumPng) throw invalid();
    await io.seek(file, offsets[4], 0);
    png = await io.read(file, length);
    if (png.length !== length) throw invalid();
  } finally { await io.close(file); }
  const view = new DataView(png.buffer, png.byteOffset, png.byteLength);
  const signature = [137,80,78,71,13,10,26,10];
  if (signature.some((byte, i) => png[i] !== byte) || view.getUint32(8) !== 13 ||
      view.getUint32(12) !== 0x49484452 || view.getUint32(16) !== width || view.getUint32(20) !== height) throw invalid();
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; ++i) {
    let value = i;
    for (let bit = 0; bit < 8; ++bit) value = (value >>> 1) ^ (value & 1 ? 0xedb88320 : 0);
    table[i] = value;
  }
  const crc32 = (data, start, end) => {
    let crc = 0xffffffff;
    for (let i = start; i < end; ++i) crc = (crc >>> 8) ^ table[(crc ^ data[i]) & 255];
    return (crc ^ 0xffffffff) >>> 0;
  };
  let at = 8, ended = false;
  while (at < png.length) {
    if (png.length - at < 12) throw invalid();
    const length = view.getUint32(at), kind = view.getUint32(at + 4);
    if (length > png.length - at - 12 || kind === 0x6163544c ||
        crc32(png, at + 4, at + 8 + length) !== view.getUint32(at + 8 + length)) throw invalid();
    at += 12 + length;
    if (kind === 0x49454e44) { ended = length === 0 && at === png.length; break; }
  }
  if (!ended) throw invalid();
  let bitmap;
  try {
    bitmap = await createImageBitmap(new Blob([png], {type: 'image/png'}),
      {colorSpaceConversion: 'none', premultiplyAlpha: 'none', imageOrientation: 'none'});
    if (bitmap.width !== width || bitmap.height !== height) throw invalid();
    const canvas = new OffscreenCanvas(width, height);
    const context = canvas.getContext('2d', {willReadFrequently: true});
    if (!context) throw invalid();
    context.fillStyle = '#000'; context.fillRect(0, 0, width, height);
    context.drawImage(bitmap, 0, 0);
    const rgba = context.getImageData(0, 0, width, height).data;
    const output = new Uint8Array(24 + width * height * 2);
    output.set([74,71,82,66,71,48,49,0]); // JGRBG01\0
    const words = new DataView(output.buffer);
    words.setUint32(8, width, true); words.setUint32(12, height, true);
    words.setUint32(16, width * height * 2, true);
    for (let i = 0; i < width * height; ++i) {
      const pixel = (rgba[4*i] >> 3) | ((rgba[4*i+1] >> 2) << 5) | ((rgba[4*i+2] >> 3) << 11);
      words.setUint16(24 + i * 2, pixel, true);
    }
    words.setUint32(20, crc32(output, 24, output.length), true);
    for (const name of ['/game/data', '/game/data/setup']) {
      try { await io.mkdir(name); }
      catch (error) { if (io.translate(error).errno !== 17 || ((await io.stat(name)).mode & 0xf000) !== 0x4000) throw error; }
    }
    const temporary = '/game/data/setup/background.jbg.tmp';
    let fd;
    try {
      fd = await io.open(temporary, 'wb');
      if (await io.write(fd, output) !== output.length) throw new Error('Could not write startup artwork.');
      await io.close(fd); fd = undefined;
      await io.rename(temporary, '/game/data/setup/background.jbg');
    } catch (error) {
      if (fd !== undefined) try { await io.close(fd); } catch (_) {}
      try { await io.remove(temporary); } catch (_) {}
      throw error;
    }
    return true;
  } finally { if (bitmap) bitmap.close(); }
};
