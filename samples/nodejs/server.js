/**
 * OUCS Node.js Streaming Server — Sample App
 * ============================================
 * HTTP server that serves songs from a .oucs file
 * as audio streams using HTTP Range requests.
 *
 * Usage:
 *   node server.js playlist.oucs [--port 3000]
 *
 * Endpoints:
 *   GET /                    — List all songs (JSON)
 *   GET /song/:index         — Stream song audio (chunked)
 *   GET /info                — Container metadata (JSON)
 *   GET /song/:index/info    — Single song metadata (JSON)
 *   GET /song/:index/waveform — Waveform data (JSON float array)
 *
 * The .oucs file is served without extracting everything up front.
 * Each song is read chunk-by-chunk directly from the container.
 *
 * License: MIT
 */

'use strict';

const http  = require('http');
const fs    = require('fs');
const path  = require('path');

// ── Parse args ────────────────────────────────────────────────

const args      = process.argv.slice(2);
const oucsPath  = args[0];
let   port      = 3000;

for (let i = 0; i < args.length; i++) {
  if (args[i] === '--port' && args[i + 1]) port = parseInt(args[i + 1]);
}

if (!oucsPath) {
  console.error('Usage: node server.js <playlist.oucs> [--port 3000]');
  process.exit(1);
}

if (!fs.existsSync(oucsPath)) {
  console.error(`File not found: ${oucsPath}`);
  process.exit(1);
}

// ── Parse .oucs header & index (pure JS) ─────────────────────

function parseMagic(buf) {
  return buf.slice(0, 4).toString('ascii');
}

function readCString(buf, offset, maxLen) {
  const end = buf.indexOf(0, offset);
  const limit = end >= 0 && end < offset + maxLen ? end : offset + maxLen;
  return buf.slice(offset, limit).toString('utf8').replace(/\0/g, '').trim();
}

function readFloat32LE(buf, offset) {
  return buf.readFloatLE(offset);
}

const INDEX_ENTRY_SIZE = 512;

function parseOucsFile(filePath) {
  const fd = fs.openSync(filePath, 'r');

  // Read header (44 bytes)
  const hdrBuf = Buffer.alloc(44);
  fs.readSync(fd, hdrBuf, 0, 44, 0);

  if (parseMagic(hdrBuf) !== 'OUCS') {
    fs.closeSync(fd);
    throw new Error('Invalid .oucs file (magic bytes mismatch)');
  }

  const songCount   = hdrBuf.readUInt32LE(8);
  const metaOffset  = Number(hdrBuf.readBigUInt64LE(12));
  const indexOffset = Number(hdrBuf.readBigUInt64LE(20));

  // Read container metadata
  let containerMeta = { theme: '', description: '', songCount };
  if (metaOffset > 0) {
    const metaBuf = Buffer.alloc(1024 + 256 + 8);
    const read = fs.readSync(fd, metaBuf, 0, metaBuf.length, metaOffset);
    if (read > 0) {
      containerMeta.theme       = readCString(metaBuf, 0, 256);
      containerMeta.description = readCString(metaBuf, 256, 1024);
    }
  }

  // Read index table
  const indexBuf = Buffer.alloc(songCount * INDEX_ENTRY_SIZE);
  fs.readSync(fd, indexBuf, 0, indexBuf.length, indexOffset);

  const songs = [];
  for (let i = 0; i < songCount; i++) {
    const base = i * INDEX_ENTRY_SIZE;
    const uuid = indexBuf.slice(base, base + 16).toString('hex');
    const name = readCString(indexBuf, base + 16, 64) || `Song ${i}`;
    const desc = readCString(indexBuf, base + 80, 256);
    const byteOffset = Number(indexBuf.readBigUInt64LE(base + 336));
    const byteSize   = Number(indexBuf.readBigUInt64LE(base + 344));
    const fmtBytes   = indexBuf.slice(base + 352, base + 356).toString('ascii').trim().replace(/\0/g, '');
    const crc32      = indexBuf.readUInt32LE(base + 356);
    const bpm        = readFloat32LE(indexBuf, base + 436);
    const musicalKey = indexBuf[base + 440];
    const moodFlags  = indexBuf[base + 441];
    const durationMs = indexBuf.readUInt32LE(base + 475);
    const sampleRate = indexBuf.readUInt32LE(base + 479);
    const channels   = indexBuf.readUInt16LE(base + 483);
    const bitrate    = indexBuf.readUInt16LE(base + 485);
    const encFlag    = indexBuf[base + 462];

    songs.push({
      index: i, uuid, name, description: desc,
      byteOffset, byteSize, audioFormat: fmtBytes,
      crc32, bpm, musicalKey, moodFlags, durationMs,
      sampleRate, channels, bitrate, encrypted: encFlag === 1,
    });
  }

  fs.closeSync(fd);
  return { containerMeta, songs, filePath };
}

// Load the .oucs file
let oucsData;
try {
  oucsData = parseOucsFile(oucsPath);
  console.log(`✓ Loaded: ${path.basename(oucsPath)}`);
  console.log(`  Theme: ${oucsData.containerMeta.theme || '(none)'}`);
  console.log(`  Songs: ${oucsData.songs.length}`);
} catch (e) {
  console.error(`Error parsing .oucs file: ${e.message}`);
  process.exit(1);
}

// ── MIME type mapping ─────────────────────────────────────────

const FORMAT_MIME = {
  'MP3': 'audio/mpeg',
  'FLAC': 'audio/flac',
  'OGG': 'audio/ogg',
  'WAV': 'audio/wav',
  'AAC': 'audio/aac',
  'OPUS': 'audio/opus',
};

// ── HTTP server ───────────────────────────────────────────────

const CHUNK_SIZE = 32768; // 32KB chunks

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://localhost:${port}`);
  const pathname = url.pathname;

  // CORS headers for browser use
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, OPTIONS');

  if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return; }

  // ── GET / — song list ──────────────────────────────────────
  if (pathname === '/') {
    const body = JSON.stringify({
      container: oucsData.containerMeta,
      songs: oucsData.songs.map(s => ({
        ...s,
        streamUrl: `/song/${s.index}`,
        infoUrl:   `/song/${s.index}/info`,
      }))
    }, null, 2);
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(body);
    return;
  }

  // ── GET /info — container metadata ────────────────────────
  if (pathname === '/info') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(oucsData.containerMeta, null, 2));
    return;
  }

  // ── GET /song/:index ──────────────────────────────────────
  const songMatch = pathname.match(/^\/song\/(\d+)$/);
  if (songMatch) {
    const idx  = parseInt(songMatch[1]);
    const song = oucsData.songs[idx];
    if (!song) { res.writeHead(404); res.end('Song not found'); return; }
    if (song.encrypted) { res.writeHead(403); res.end('Song is encrypted'); return; }

    const mime     = FORMAT_MIME[song.audioFormat] || 'audio/mpeg';
    const fileSize = song.byteSize;

    // Handle HTTP Range requests
    const rangeHeader = req.headers.range;
    let startByte = song.byteOffset;
    let endByte   = song.byteOffset + fileSize - 1;
    let status    = 200;

    if (rangeHeader) {
      const match = rangeHeader.match(/bytes=(\d*)-(\d*)/);
      if (match) {
        const rangeStart = match[1] ? parseInt(match[1]) : 0;
        const rangeEnd   = match[2] ? parseInt(match[2]) : fileSize - 1;
        startByte = song.byteOffset + rangeStart;
        endByte   = song.byteOffset + rangeEnd;
        status    = 206;
        const contentLength = endByte - startByte + 1;
        res.setHeader('Content-Range', `bytes ${rangeStart}-${rangeEnd}/${fileSize}`);
        res.setHeader('Content-Length', contentLength);
      }
    } else {
      res.setHeader('Content-Length', fileSize);
    }

    res.setHeader('Content-Type', mime);
    res.setHeader('Accept-Ranges', 'bytes');
    res.setHeader('X-OUCS-Song-Name', song.name);
    res.setHeader('X-OUCS-BPM', song.bpm.toFixed(1));
    res.writeHead(status);

    // Stream song bytes from .oucs file chunk by chunk
    const fd = fs.openSync(oucsData.filePath, 'r');
    let pos = startByte;
    const chunkBuf = Buffer.alloc(CHUNK_SIZE);

    function streamNext() {
      if (pos > endByte || res.destroyed) { fs.closeSync(fd); return; }
      const toRead = Math.min(CHUNK_SIZE, endByte - pos + 1);
      const n = fs.readSync(fd, chunkBuf, 0, toRead, pos);
      if (n <= 0) { fs.closeSync(fd); res.end(); return; }
      pos += n;
      const ok = res.write(chunkBuf.slice(0, n));
      if (ok) {
        setImmediate(streamNext);
      } else {
        res.once('drain', streamNext);
      }
    }
    streamNext();
    res.on('close', () => { try { fs.closeSync(fd); } catch (_) {} });
    return;
  }

  // ── GET /song/:index/info ─────────────────────────────────
  const infoMatch = pathname.match(/^\/song\/(\d+)\/info$/);
  if (infoMatch) {
    const idx  = parseInt(infoMatch[1]);
    const song = oucsData.songs[idx];
    if (!song) { res.writeHead(404); res.end('Song not found'); return; }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ...song, streamUrl: `/song/${idx}` }, null, 2));
    return;
  }

  res.writeHead(404);
  res.end('Not found');
});

server.listen(port, () => {
  console.log(`\n🎵 OUCS Streaming Server running at http://localhost:${port}`);
  console.log(`   Song list  : http://localhost:${port}/`);
  console.log(`   Stream song: http://localhost:${port}/song/0`);
  console.log(`   Song info  : http://localhost:${port}/song/0/info`);
  console.log('\nPress Ctrl+C to stop.\n');
});
