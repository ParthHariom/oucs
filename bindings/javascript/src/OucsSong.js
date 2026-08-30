'use strict';

/**
 * OucsSong — represents a single song entry in a .oucs file.
 * Obtained via OucsFile.songs() or OucsFile.songInfo(index).
 */
class OucsSong {
  constructor(file, index, info) {
    this._file  = file;
    this._index = index;
    this._info  = info;
  }

  get index()       { return this._index; }
  get name()        { return this._info.name; }
  get uuid()        { return this._info.uuid; }
  get format()      { return this._info.audioFormat; }
  get byteSize()    { return this._info.byteSize; }
  get bpm()         { return this._info.bpm; }
  get encrypted()   { return this._info.encrypted; }
  get durationMs()  { return this._info.durationMs; }
  get info()        { return this._info; }

  /** Open a stream for this song. Returns Promise<OucsStream>. */
  async stream(chunkSize = 4096, password = null) {
    return this._file.stream(this._index, chunkSize, password);
  }

  /** Extract full song bytes. Returns Promise<Uint8Array>. */
  async extractBytes(password = null) {
    return this._file.extractBytes(this._index, password);
  }

  toString() {
    return `OucsSong(${this._index}: "${this.name}" ${this.format} ${this.byteSize} bytes)`;
  }
}

module.exports = { OucsSong };
