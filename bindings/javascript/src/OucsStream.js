/**
 * OucsStream — async chunk-by-chunk song streaming.
 * Implements AsyncIterable so you can use for-await-of.
 */
'use strict';

const { getModule }   = require('./wasm');
const { raiseForCode } = require('./errors');

const DEFAULT_CHUNK = 4096;

class OucsStream {
  /**
   * @param {number} readerPtr  - WASM pointer to OucsReader
   * @param {number} songIndex  - Song index to stream
   * @param {number} chunkSize  - Bytes per chunk
   * @param {string|null} password - Decryption password
   */
  constructor(readerPtr, songIndex, chunkSize = DEFAULT_CHUNK, password = null) {
    this._readerPtr = readerPtr;
    this._songIndex = songIndex;
    this._chunkSize = chunkSize;
    this._password  = password;
    this._ptr       = null;
    this._bufPtr    = null;
    this._m         = null;
  }

  async _init() {
    this._m = getModule();
    const passPtr = this._password
      ? this._m.allocateUTF8(this._password) : 0;

    this._ptr = this._m._oucs_stream_open(
      this._readerPtr, this._songIndex, this._chunkSize, passPtr
    );
    if (passPtr) this._m._free(passPtr);
    if (!this._ptr) throw new Error(`Cannot open stream for song ${this._songIndex}`);

    this._bufPtr = this._m._malloc(this._chunkSize);
    if (!this._bufPtr) throw new Error('Out of WASM memory');
    return this;
  }

  /** Read next chunk. Returns Uint8Array or null at EOF. */
  readChunk() {
    if (!this._ptr) return null;
    const sizePtr = this._m._malloc(4);
    const ret = this._m._oucs_stream_read_chunk(
      this._ptr, this._bufPtr, this._chunkSize, sizePtr
    );
    raiseForCode(ret, 'stream_read_chunk');
    const bytesRead = this._m.HEAP32[sizePtr >> 2];
    this._m._free(sizePtr);
    if (bytesRead === 0) return null;
    // Copy from WASM heap
    return new Uint8Array(this._m.HEAPU8.buffer, this._bufPtr, bytesRead).slice();
  }

  /** Seek to byte offset within song. */
  seek(byteOffset) {
    raiseForCode(this._m._oucs_stream_seek(this._ptr, byteOffset), 'stream_seek');
  }

  /** Seek to chapter by index. */
  seekChapter(chapterIndex) {
    raiseForCode(
      this._m._oucs_stream_seek_chapter(this._ptr, chapterIndex),
      'stream_seek_chapter'
    );
  }

  /** Current byte position. */
  get position() {
    return this._ptr ? this._m._oucs_stream_tell(this._ptr) : 0;
  }

  close() {
    if (this._ptr)    { this._m._oucs_stream_free(this._ptr); this._ptr = null; }
    if (this._bufPtr) { this._m._free(this._bufPtr); this._bufPtr = null; }
  }

  /** AsyncIterator — yields Uint8Array chunks. */
  async *[Symbol.asyncIterator]() {
    try {
      while (true) {
        const chunk = this.readChunk();
        if (!chunk) break;
        yield chunk;
      }
    } finally {
      this.close();
    }
  }

  /** Read all audio data into a single Uint8Array. */
  async readAll() {
    const chunks = [];
    let total = 0;
    for await (const chunk of this) {
      chunks.push(chunk);
      total += chunk.length;
    }
    const result = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
      result.set(chunk, offset);
      offset += chunk.length;
    }
    return result;
  }
}

module.exports = { OucsStream };
