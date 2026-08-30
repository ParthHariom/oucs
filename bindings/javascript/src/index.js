/**
 * OUCS JavaScript Bindings — Entry Point
 * Works in both Node.js and browser (via WASM).
 *
 * Usage (Node.js):
 *   const { OucsFile } = require('oucs');
 *   const f = await OucsFile.open('playlist.oucs');
 *   for (const song of f.songs()) {
 *     for await (const chunk of song.stream()) {
 *       player.feed(chunk);
 *     }
 *   }
 *
 * Usage (Browser):
 *   import { OucsFile } from 'oucs';
 *   const f = await OucsFile.openBytes(arrayBuffer);
 *   const stream = f.song(0).stream();
 *   const mediaSource = new MediaSource();
 *   // feed chunks to MediaSource API
 *
 * License: MIT
 */

'use strict';

const { OucsFile }    = require('./OucsFile');
const { OucsSong }    = require('./OucsSong');
const { OucsStream }  = require('./OucsStream');
const { OucsError,
        OucsNotFoundError,
        OucsCorruptError } = require('./errors');
const { loadModule }  = require('./wasm');

module.exports = {
  OucsFile,
  OucsSong,
  OucsStream,
  OucsError,
  OucsNotFoundError,
  OucsCorruptError,
  loadModule,
  version: '1.0.0',
};
