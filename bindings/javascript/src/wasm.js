/**
 * WASM module loader.
 * In Node.js: loads oucs.js (Emscripten output) from dist/
 * In browser: dynamically imports oucs.js
 */
'use strict';

let _module = null;

async function loadModule() {
  if (_module) return _module;

  let OucsModule;
  if (typeof process !== 'undefined' && process.versions && process.versions.node) {
    // Node.js environment
    try {
      OucsModule = require('../dist/oucs.js');
    } catch (e) {
      throw new Error(
        'liboucs WASM not found. Build it first:\n' +
        '  cd bindings/javascript && npm run build:wasm\n' +
        'Or set OUCS_WASM_PATH env variable.'
      );
    }
  } else {
    // Browser environment — assume oucs.js loaded via <script> or bundler
    if (typeof OucsModule === 'undefined') {
      throw new Error('OucsModule not found. Load dist/oucs.js before using this library.');
    }
  }

  _module = await OucsModule();
  return _module;
}

function getModule() {
  if (!_module) throw new Error('WASM module not loaded. Call await loadModule() first.');
  return _module;
}

module.exports = { loadModule, getModule };
