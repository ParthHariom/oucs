# OUCS Engine

> **Open Universal Container for Sound**
> *"Pack once, stream anything, everywhere."*

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](CHANGELOG.md)
[![CI](https://github.com/oucs-engine/oucs/actions/workflows/ci.yml/badge.svg)](https://github.com/oucs-engine/oucs/actions)

OUCS is a binary audio container format (`.oucs`) that packs multiple audio files into a single, self-describing file — with **selective unit-by-unit streaming**, embedded metadata, AES-256 encryption, Reed-Solomon error correction, and zero-dependency multi-language bindings.

---

## Why OUCS?

| Solution | Problem |
|----------|---------|
| ZIP/TAR | Full extraction required |
| CUE sheets | Separate files required |
| HLS/DASH | Server infrastructure required |
| **OUCS** | ✅ Single file · selective stream · encrypted · offline + CDN · any platform |

---

## Feature Highlights

- **Selective streaming** — only the target song loads, chunk by chunk. Device RAM is never pressured.
- **AES-256-GCM encryption** — per-song or whole container, PBKDF2 key derivation.
- **Reed-Solomon ECC** — corrupt bytes are auto-corrected on read.
- **Rich metadata** — BPM, musical key, mood, synced lyrics (LRC), chapters, waveform preview, acoustic fingerprint, language, accessibility transcript.
- **HTTP Range requests** — stream directly from a CDN URL, no full download.
- **Offline-first sync manifest** — multi-device playlist sync, no cloud dependency.
- **Plugin/hook system** — intercept pack, chunk-read, extract, and error events.
- **Merge & split** — combine or divide `.oucs` files without re-encoding.
- **Append-only version history** — add songs without rewriting the file.
- **Deduplication** — same song stored once, referenced multiple times.
- **Multi-language** — C core → Python, JavaScript (WASM + Node.js), Java (JNI/Android).

---

## Quick Start

### Build the C core

```bash
git clone https://github.com/oucs-engine/oucs
cd oucs
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### CLI

```bash
# Pack songs
./build/oucs pack playlist.oucs --name "My Playlist" song1.mp3 song2.flac

# Info
./build/oucs info playlist.oucs

# Extract one song (no full extraction of others)
./build/oucs extract playlist.oucs 0 song0.mp3

# Stream (chunk-by-chunk, low memory)
./build/oucs stream playlist.oucs 1

# Merge two playlists
./build/oucs merge combined.oucs a.oucs b.oucs

# Find duplicates
./build/oucs dedup playlist.oucs
```

### Python

```bash
pip install oucs
```

```python
from oucs import OucsFile

# Pack
with OucsFile.create("playlist.oucs") as f:
    f.set_meta(theme="Summer Hits", description="Hot tracks")
    f.add_song("track1.mp3", name="Track One")
    f.add_song("track2.flac", name="Track Two", password="secret")
    f.save()

# Stream
with OucsFile.open("playlist.oucs") as f:
    print(f.info())
    stream = f.stream(0, chunk_size=4096)
    for chunk in stream:
        audio_player.feed(chunk)

# URL streaming (no download)
f = OucsFile.open_url("https://cdn.example.com/playlist.oucs")
for chunk in f.stream(0):
    player.feed(chunk)
```

### JavaScript (Node.js)

```bash
npm install oucs
```

```js
const { OucsFile, loadModule } = require('oucs');

await loadModule();
const f = await OucsFile.open('playlist.oucs');
console.log(f.songCount, 'songs');

const stream = await f.stream(0, 4096);
for await (const chunk of stream) {
  audioPlayer.feed(chunk);
}
f.close();
```

### JavaScript (Browser)

```html
<script src="dist/oucs.js"></script>
<script>
  OucsModule().then(m => {
    // Use m._oucs_* functions directly, or use the OucsFile wrapper
  });
</script>
```

See `samples/web/index.html` for a full web player demo.

### Java

```java
OucsEngine.loadLibrary();

// Write
try (OucsWriter w = OucsEngine.create("playlist.oucs")) {
    w.setMeta("My Playlist", "Weekend vibes", null, "");
    w.addSong("song1.mp3", "Song One", "");
    w.save();
}

// Read & stream
try (OucsReader r = OucsEngine.open("playlist.oucs")) {
    System.out.println(r.getSongCount() + " songs");
    try (OucsStream s = r.stream(0, 4096)) {
        byte[] chunk;
        while ((chunk = s.readChunk()) != null) {
            audioPlayer.feed(chunk);
        }
    }
}
```

---

## `.oucs` File Structure

```
┌─────────────────────────────────────┐
│  FILE HEADER (44 bytes)             │
│  Magic "OUCS" · version · flags     │
│  song_count · block offsets         │
├─────────────────────────────────────┤
│  CONTAINER METADATA BLOCK           │
│  theme · description · logo         │
│  logo_url · created_at · crc32      │
├─────────────────────────────────────┤
│  INDEX TABLE (song_count × 512 B)   │
│  per song: uuid · name · offset     │
│  size · format · crc32 · ecc_ptr    │
│  waveform · fingerprint · lyrics    │
│  chapters · BPM · key · mood        │
│  accessibility · encryption         │
├─────────────────────────────────────┤
│  SONG UNIT 0..N                     │
│  audio bytes · ECC parity           │
│  lyrics block · waveform data       │
│  fingerprint · chapters             │
│  accessibility block                │
├─────────────────────────────────────┤
│  SYNC MANIFEST (optional)           │
│  device list · changelog            │
├─────────────────────────────────────┤
│  VERSION HISTORY (optional)         │
│  append-only entry log              │
└─────────────────────────────────────┘
```

Full byte-level specification: [`SPEC.md`](SPEC.md)

---

## Multi-language Architecture

```
C Core (liboucs)
    │
    ├── Python  ──→ ctypes/CFFI   ──→ pip install oucs
    ├── JS      ──→ WASM (Emscripten) + Node N-API  ──→ npm install oucs
    └── Java    ──→ JNI           ──→ Maven io.oucs:oucs:1.0.0
```

---

## Project Layout

```
oucs/
├── include/
│   └── oucs_format.h          # Public C API header
├── src/
│   ├── oucs_encoder.c         # Writer / pack
│   ├── oucs_decoder.c         # Reader / extract
│   ├── oucs_stream.c          # Chunk-by-chunk streaming
│   ├── oucs_ecc.c             # Reed-Solomon error correction
│   ├── oucs_crypto.c          # AES-256-GCM + PBKDF2
│   ├── oucs_analysis.c        # BPM / key / mood / waveform / fingerprint
│   ├── oucs_delta.c           # Dedup + delta encoding
│   ├── oucs_history.c         # Version history + sync manifest
│   ├── oucs_hooks.c           # Plugin/hook system
│   ├── oucs_network.c         # HTTP Range streaming
│   └── oucs_util.c            # UUID, errors, helpers
├── cli/
│   └── oucs_cli.c             # CLI: pack extract info merge split dedup history stream
├── bindings/
│   ├── python/oucs/           # Python bindings (ctypes)
│   ├── javascript/src/        # JS/WASM bindings
│   └── java/                  # Java/JNI bindings + pom.xml
├── samples/
│   ├── python/player.py       # Python CLI player demo
│   ├── web/index.html         # Browser web player
│   └── nodejs/server.js       # Node.js streaming server
├── tests/
│   └── test_oucs.c            # C test suite
├── CMakeLists.txt             # Build system
├── SPEC.md                    # Binary format specification
└── README.md                  # This file
```

---

## Build Options

| CMake Option | Default | Description |
|---|---|---|
| `OUCS_BUILD_CLI` | ON | Build `oucs` CLI tool |
| `OUCS_BUILD_TESTS` | ON | Build test suite |
| `OUCS_USE_LIBSODIUM` | OFF | Use libsodium for crypto (hardware-accelerated) |
| `OUCS_USE_TLS` | OFF | Enable HTTPS (requires mbedTLS) |
| `OUCS_BUILD_WASM` | OFF | Build WebAssembly target (requires Emscripten) |

```bash
# With libsodium
cmake -B build -DOUCS_USE_LIBSODIUM=ON && cmake --build build

# WASM build
cmake -B build-wasm -DOUCS_BUILD_WASM=ON && cmake --build build-wasm
# or via npm:
cd bindings/javascript && npm run build:wasm
```

---

## Error Handling

All C API functions return `OucsError` (int). Zero = success, negative = error.

```c
OucsReader *r = oucs_reader_open("playlist.oucs");
if (!r) { fprintf(stderr, "open failed\n"); exit(1); }

int ret = oucs_reader_extract_song(r, 0, "out.mp3", NULL);
if (ret != OUCS_OK) {
    fprintf(stderr, "Error: %s\n", oucs_strerror(ret));
}
```

| Code | Constant | Meaning |
|------|---------|---------|
| 0 | `OUCS_OK` | Success |
| -2 | `OUCS_ERR_IO` | File I/O error |
| -3 | `OUCS_ERR_INVALID_MAGIC` | Not a .oucs file |
| -5 | `OUCS_ERR_CORRUPT` | File corrupted |
| -6 | `OUCS_ERR_NOT_FOUND` | Song not found |
| -9 | `OUCS_ERR_WRONG_PASSWORD` | Wrong decryption password |
| -10 | `OUCS_ERR_ECC_FAIL` | Too many errors to correct |

---

## Hook System

```c
// Register a hook to monitor every chunk during streaming
void my_chunk_hook(const uint8_t *chunk, size_t size, void *userdata) {
    printf("Chunk: %zu bytes\n", size);
}

OucsStream *s = oucs_stream_open(reader, 0, 4096, NULL);
oucs_stream_register_hook(s, OUCS_HOOK_CHUNK_READ, my_chunk_hook, NULL);

// Or register globally (applies to all streams)
oucs_hook_register_global(OUCS_HOOK_ON_ERROR, my_error_handler, NULL);
```

---

## Running Tests

```bash
cmake -B build -DOUCS_BUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

---

## Sample Apps

| App | Location | Description |
|-----|----------|-------------|
| Python CLI player | `samples/python/player.py` | Pack, info, stream, waveform, lyrics |
| Web player | `samples/web/index.html` | Drag-drop .oucs → full browser player |
| Node.js server | `samples/nodejs/server.js` | HTTP streaming server for .oucs files |

### Run the web player

```bash
# Open directly in browser (no server needed for local .oucs files)
open samples/web/index.html
```

### Run the Node.js server

```bash
node samples/nodejs/server.js playlist.oucs --port 3000
# Streams songs at http://localhost:3000/song/0
```

### Run the Python player

```bash
python samples/python/player.py pack playlist.oucs song1.mp3 song2.mp3
python samples/python/player.py play playlist.oucs 0
```

---

## Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make changes (C core first, then bindings)
4. Run tests: `cd build && ctest`
5. Submit a pull request

Please keep PRs focused. One feature or fix per PR.

---

## License

MIT License — see [LICENSE](LICENSE) for full text.

---

## Roadmap

- [ ] v1.1: libsodium backend (hardware AES)
- [ ] v1.2: Chromaprint integration (production fingerprinting)
- [ ] v1.3: Swift bindings (iOS/macOS native)
- [ ] v1.4: Rust bindings
- [ ] v2.0: Streaming delta compression

---

*OUCS Engine — Built for developers, designed for music.*
