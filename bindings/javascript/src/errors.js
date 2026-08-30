'use strict';

class OucsError extends Error {
  constructor(message, code = 0) {
    super(message);
    this.name = 'OucsError';
    this.code = code;
  }
}

class OucsNotFoundError extends OucsError {
  constructor(msg) { super(msg, -6); this.name = 'OucsNotFoundError'; }
}

class OucsCorruptError extends OucsError {
  constructor(msg) { super(msg, -5); this.name = 'OucsCorruptError'; }
}

class OucsCryptoError extends OucsError {
  constructor(msg) { super(msg, -9); this.name = 'OucsCryptoError'; }
}

class OucsIOError extends OucsError {
  constructor(msg) { super(msg, -2); this.name = 'OucsIOError'; }
}

function raiseForCode(code, context = '') {
  if (code === 0) return;
  const msg = context ? `${context}: error code ${code}` : `error code ${code}`;
  if (code === -6) throw new OucsNotFoundError(msg);
  if (code === -5 || code === -3 || code === -10) throw new OucsCorruptError(msg);
  if (code === -8 || code === -9) throw new OucsCryptoError(msg);
  if (code === -2) throw new OucsIOError(msg);
  throw new OucsError(msg, code);
}

module.exports = { OucsError, OucsNotFoundError, OucsCorruptError, OucsCryptoError, OucsIOError, raiseForCode };
