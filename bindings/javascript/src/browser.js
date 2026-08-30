/**
 * OUCS Browser Entry Point
 *
 * In the browser, the WASM module (dist/oucs.js) must be loaded
 * via a <script> tag before using this library.
 *
 * Usage:
 *   <script src="dist/oucs.js"></script>
 *   <script src="src/browser.js"></script>
 *   <script>
 *     OucsModule().then(m => {
 *       // use m._oucs_* functions or the OucsFile wrapper
 *     });
 *   </script>
 *
 * Or use the pure-JS parser (no WASM needed for reading .oucs files):
 *   import { OucsFile } from 'oucs';
 *   const f = await OucsFile.openBytes(arrayBuffer);
 */

'use strict';

const { OucsFile }   = require('./OucsFile');
const { OucsStream } = require('./OucsStream');
const { OucsError, OucsNotFoundError, OucsCorruptError } = require('./errors');

module.exports = { OucsFile, OucsStream, OucsError, OucsNotFoundError, OucsCorruptError };
