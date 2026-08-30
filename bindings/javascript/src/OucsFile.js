/**
 * OucsFile — main JS/WASM interface for .oucs files.
 */
'use strict';

const { loadModule, getModule } = require('./wasm');
const { OucsStream }            = require('./OucsStream');
const { raiseForCode, OucsIOError } = require('./errors');

const INDEX_ENTRY_SIZE = 512;

class OucsFile {
  constructor() {
    this._ptr       = null;
    this._m         = null;
    this._songCount = 0;
    this._meta      = {};
  }

  // ── Factory methods ─────────────────────────────────────────

  /**
   * Open a .oucs file (Node.js only).
   * @param {string} filePath
   * @returns {Promise<OucsFile>}
   */
  static async open(filePath) {
    await loadModule();
    const m   = getModule();
    const f   = new OucsFile();
    f._m      = m;
    const pathPtr = m.allocateUTF8(filePath);
    f._ptr    = m._oucs_reader_open(pathPtr);
    m._free(pathPtr);
    if (!f._ptr) throw new OucsIOError(`Cannot open: ${filePath}`);
    f._songCount = m._oucs_reader_song_count(f._ptr);
    return f;
  }

  /**
   * Open a .oucs file from an ArrayBuffer (browser/Node).
   * @param {ArrayBuffer} buffer
   * @returns {Promise<OucsFile>}
   */
  static async openBytes(buffer) {
    await loadModule();
    const m = getModule();
    // Write to WASM memory as a temp file via FS (Emscripten FS API)
    const arr   = new Uint8Array(buffer);
    const path  = '/tmp/oucs_input.oucs';
    m.FS.writeFile(path, arr);
    return OucsFile.open(path);
  }

  /**
   * Open a .oucs file from a URL (HTTP Range streaming).
   * @param {string} url
   * @returns {Promise<OucsFile>}
   */
  static async openUrl(url) {
    await loadModule();
    const m = getModule();
    const f = new OucsFile();
    f._m    = m;
    const urlPtr = m.allocateUTF8(url);
    f._ptr  = m._oucs_reader_open_url(urlPtr);
    m._free(urlPtr);
    if (!f._ptr) throw new OucsIOError(`Cannot open URL: ${url}`);
    f._songCount = m._oucs_reader_song_count(f._ptr);
    return f;
  }

  // ── Song access ──────────────────────────────────────────────

  /** Number of songs. */
  get songCount() { return this._songCount; }

  /** Get song info by index. Returns a plain object with metadata. */
  songInfo(index) {
    if (index < 0 || index >= this._songCount)
      throw new RangeError(`Song index ${index} out of range`);
    // Read index entry from WASM heap via oucs_reader_get_song_info
    // We use a raw buffer approach since JS has no C struct access
    return {
      index,
      name: `Song ${index}`,  // populated by full struct binding
    };
  }

  /** Iterate over all songs as plain info objects. */
  *songs() {
    for (let i = 0; i < this._songCount; i++) {
      yield this.songInfo(i);
    }
  }

  // ── Streaming ────────────────────────────────────────────────

  /**
   * Open an async stream for a song.
   * @param {number} index       Song index
   * @param {number} chunkSize   Bytes per chunk (default 4096)
   * @param {string|null} password  Decryption password
   * @returns {Promise<OucsStream>}
   */
  async stream(index, chunkSize = 4096, password = null) {
    if (index < 0 || index >= this._songCount)
      throw new RangeError(`Song index ${index} out of range`);
    const s = new OucsStream(this._ptr, index, chunkSize, password);
    await s._init();
    return s;
  }

  // ── Extraction ───────────────────────────────────────────────

  /**
   * Extract a song to memory.
   * @param {number} index
   * @param {string|null} password
   * @returns {Promise<Uint8Array>}
   */
  async extractBytes(index, password = null) {
    const m = this._m;
    const dataPtrPtr = m._malloc(4);
    const sizePtrVal = m._malloc(4);
    const passPtr = password ? m.allocateUTF8(password) : 0;

    const ret = m._oucs_reader_extract_song_mem(
      this._ptr, index, dataPtrPtr, sizePtrVal, passPtr
    );
    if (passPtr) m._free(passPtr);
    raiseForCode(ret, `extractBytes(${index})`);

    const dataPtr = m.HEAP32[dataPtrPtr >> 2];
    const size    = m.HEAP32[sizePtrVal >> 2];
    const result  = new Uint8Array(m.HEAPU8.buffer, dataPtr, size).slice();

    m._free(dataPtr);
    m._free(dataPtrPtr);
    m._free(sizePtrVal);
    return result;
  }

  // ── Lifecycle ────────────────────────────────────────────────

  close() {
    if (this._ptr && this._m) {
      this._m._oucs_reader_free(this._ptr);
      this._ptr = null;
    }
  }

  [Symbol.dispose]() { this.close(); }
}

module.exports = { OucsFile };
