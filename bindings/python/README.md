# OUCS Engine

> **Open Universal Container for Sound**
> *"Pack once, stream anything, everywhere."*

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](https://github.com/ParthHariom/oucs/releases)
[![CI](https://github.com/ParthHariom/oucs/actions/workflows/ci.yml/badge.svg)](https://github.com/ParthHariom/oucs/actions)
[![PyPI](https://img.shields.io/pypi/v/oucs.svg)](https://pypi.org/project/oucs/)
[![npm](https://img.shields.io/npm/v/oucs-engine.svg)](https://www.npmjs.com/package/oucs-engine)
[![Demo](https://img.shields.io/badge/demo-live-brightgreen)](https://oucs-engine.vercel.app)

> 🌐 **[Live Demo — oucs-engine.vercel.app](https://oucs-engine.vercel.app)** — Try the player & creator in your browser, no install needed.

OUCS is a binary audio container format (`.oucs`) that packs multiple audio files into a single, self-describing file — with **selective unit-by-unit streaming**, embedded metadata, AES-256 encryption, Reed-Solomon error correction, and zero-dependency multi-language bindings.

---

## Why OUCS?

| Solution | Problem |
|----------|---------|
| ZIP/TAR | Full extraction required before use |
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

### 1. Clone & Build (C core)

```bash
git clone https://github.com/ParthHariom/oucs.git
cd oucs
```

**macOS / Linux (no cmake needed — direct gcc/clang):**
```bash
mkdir -p build

# Build CLI tool
cc -std=c11 -I include \
  src/oucs_encoder.c src/oucs_decoder.c src/oucs_stream.c \
  src/oucs_ecc.c src/oucs_crypto.c src/oucs_analysis.c \
  src/oucs_delta.c src/oucs_history.c src/oucs_hooks.c \
  src/oucs_network.c src/oucs_util.c \
  cli/oucs_cli.c -lm -o build/oucs

# Build shared library
cc -std=c11 -shared -fPIC -I include \
  src/oucs_encoder.c src/oucs_decoder.c src/oucs_stream.c \
  src/oucs_ecc.c src/oucs_crypto.c src/oucs_analysis.c \
  src/oucs_delta.c src/oucs_history.c src/oucs_hooks.c \
  src/oucs_network.c src/oucs_util.c \
  -lm -o build/liboucs.dylib   # macOS
  # -o build/liboucs.so        # Linux
```

**With CMake (Windows / cross-platform):**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

### 2. CLI

```bash
# Pack audio files into a .oucs container
./build/oucs pack playlist.oucs --name "My Playlist" song1.mp3 song2.mp3 song3.wav

# Show container info + all songs
./build/oucs info playlist.oucs

# Extract ONE song (others are never touched)
./build/oucs extract playlist.oucs 1 output.mp3

# Stream a song chunk-by-chunk (low memory, VU meter live)
./build/oucs stream playlist.oucs 0 --chunk-size 8192

# Merge two playlists into one
./build/oucs merge combined.oucs a.oucs b.oucs

# Split: extract songs 0 to 2 into a new file
./build/oucs split playlist.oucs 0 2 first3.oucs

# Find duplicate songs
./build/oucs dedup playlist.oucs

# View version history
./build/oucs history playlist.oucs
```

---

### 3. Python

> Install after building the shared library (`build/liboucs.dylib` or `build/liboucs.so`)

```bash
cd bindings/python
pip install -e .
```

```python
from oucs import OucsFile

# ── Create a .oucs file ──────────────────────────────────────
with OucsFile.create("playlist.oucs") as f:
    f.set_meta(theme="Summer Hits", description="Hot tracks 2025")
    f.add_song("track1.mp3", name="Track One")
    f.add_song("track2.wav", name="Track Two")
    f.add_song("track3.flac", name="Track Three", password="secret")
    f.save()

# ── Read & stream ────────────────────────────────────────────
with OucsFile.open("playlist.oucs") as f:
    print(f.meta)                  # container info
    print(f.song_count, "songs")

    # Stream song 0 chunk-by-chunk (device load = near zero)
    with f.stream(0, chunk_size=4096) as stream:
        for chunk in stream:
            audio_player.feed(chunk)

# ── URL streaming (no full download) ────────────────────────
with OucsFile.open_url("https://cdn.example.com/playlist.oucs") as f:
    for chunk in f.stream(0):
        player.feed(chunk)
```

---

### 4. JavaScript (Node.js)

```bash
cd bindings/javascript
npm install oucs-engine
```

```js
const { OucsFile, loadModule } = require('./src/index');

await loadModule();                          // load WASM engine

const f = await OucsFile.open('playlist.oucs');
console.log(f.songCount + ' songs');

// Stream song 0 chunk-by-chunk
const stream = await f.stream(0, 4096);
for await (const chunk of stream) {
    audioPlayer.feed(chunk);
}
f.close();
```

**Browser (drag & drop player):**
```html
<!-- Open this file directly in any browser — no server needed -->
samples/web/index.html
```

---

### 5. JavaScript (Browser — no install)

Open `samples/web/index.html` directly in your browser.
Drag a `.oucs` file onto it — songs list appears, click to play.

---

### 6. Java

```java
// Load native library
OucsEngine.loadLibrary();   // or: System.loadLibrary("oucs")

// ── Write ────────────────────────────────────────────────────
try (OucsWriter w = OucsEngine.create("playlist.oucs")) {
    w.setMeta("My Playlist", "Best tracks", null, "");
    w.addSong("song1.mp3", "Song One", "");
    w.addSong("song2.wav", "Song Two", "my-password");  // encrypted
    w.save();
}

// ── Read & stream ────────────────────────────────────────────
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

### 7. Browser Test Lab (no install needed)

A fully working browser-based test environment is included:

```bash
# Start local server
cd public_test
python3 -m http.server 8080
```

Then open **http://localhost:8080** — two pages:

| Page | What it does |
|------|-------------|
| **Create** (`create.html`) | Drag audio files → set metadata → download `.oucs` |
| **Player** (`player.html`) | Drop `.oucs` → browse songs → play / next / prev |

No installation required. Works in any modern browser.

---

## `.oucs` File Structure

```
┌─────────────────────────────────────────┐
│  FILE HEADER (44 bytes)                 │
│  Magic "OUCS" · version · flags         │
│  song_count · block offsets             │
├─────────────────────────────────────────┤
│  CONTAINER METADATA BLOCK               │
│  theme · description · logo             │
│  logo_url · created_at · crc32          │
├─────────────────────────────────────────┤
│  INDEX TABLE (song_count × 512 bytes)   │
│  per song: uuid · name · byte_offset    │
│  size · format · crc32 · ecc_ptr        │
│  waveform · fingerprint · lyrics        │
│  chapters · BPM · key · mood            │
│  accessibility · encryption flag        │
├─────────────────────────────────────────┤
│  SONG UNIT 0..N                         │
│  audio bytes · ECC parity               │
│  lyrics block · waveform data           │
│  fingerprint · chapters                 │
│  accessibility block                    │
├─────────────────────────────────────────┤
│  SYNC MANIFEST (optional)               │
│  device list · changelog                │
├─────────────────────────────────────────┤
│  VERSION HISTORY (optional)             │
│  append-only entry log                  │
└─────────────────────────────────────────┘
```

Full byte-level specification: [`SPEC.md`](SPEC.md)

---

## Multi-language Architecture

```
C Core (liboucs)
    │
    ├── Python  ──→ ctypes/CFFI   ──→ pip install oucs
    ├── JS      ──→ WASM (Emscripten) + Node N-API  ──→ npm install oucs-engine
    └── Java    ──→ JNI           ──→ Maven io.oucs:oucs:1.0.0
```

---

## Project Layout

```
oucs/
├── include/
│   └── oucs_format.h          # Public C API — all structs & function declarations
├── src/
│   ├── oucs_encoder.c         # Writer: pack songs into .oucs
│   ├── oucs_decoder.c         # Reader: selective extract by index/UUID
│   ├── oucs_stream.c          # Chunk-by-chunk streaming (low memory)
│   ├── oucs_ecc.c             # Reed-Solomon RS(255,223) error correction
│   ├── oucs_crypto.c          # AES-256-GCM + PBKDF2-SHA256 (zero deps)
│   ├── oucs_analysis.c        # BPM / musical key / mood / waveform / fingerprint
│   ├── oucs_delta.c           # Deduplication + binary delta encoding
│   ├── oucs_history.c         # Append-only version history + sync manifest
│   ├── oucs_hooks.c           # Plugin/hook system + built-in example plugins
│   ├── oucs_network.c         # HTTP Range request streaming (CDN support)
│   └── oucs_util.c            # UUID v4, error strings, I/O helpers
├── cli/
│   └── oucs_cli.c             # CLI: pack · extract · info · merge · split · dedup · history · stream
├── bindings/
│   ├── python/oucs/           # Python bindings (ctypes, zero extra deps)
│   ├── javascript/src/        # JS/WASM bindings (Emscripten + Node N-API)
│   └── java/                  # Java/JNI bindings + Maven pom.xml
├── samples/
│   ├── python/player.py       # Python CLI player: pack · info · play · waveform · lyrics
│   ├── web/index.html         # Browser drag-drop player (no install, no server)
│   └── nodejs/server.js       # Node.js HTTP streaming server for .oucs files
├── public_test/
│   ├── index.html             # Test lab home page
│   ├── create.html            # Browser .oucs creator (drag audio → download .oucs)
│   └── player.html            # Browser .oucs player (drop .oucs → play songs)
├── tests/
│   └── test_oucs.c            # C test suite (12 tests, 0 failures)
├── CMakeLists.txt             # Cross-platform build system
├── SPEC.md                    # Full binary format specification
└── README.md                  # This file
```

---

## Build Options (CMake)

| CMake Option | Default | Description |
|---|---|---|
| `OUCS_BUILD_CLI` | ON | Build `oucs` CLI tool |
| `OUCS_BUILD_TESTS` | ON | Build test suite |
| `OUCS_USE_LIBSODIUM` | OFF | Hardware-accelerated crypto via libsodium |
| `OUCS_USE_TLS` | OFF | HTTPS support (requires mbedTLS) |
| `OUCS_BUILD_WASM` | OFF | WebAssembly target (requires Emscripten) |

```bash
# With libsodium (hardware AES)
cmake -B build -DOUCS_USE_LIBSODIUM=ON && cmake --build build

# WASM build
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
oucs_reader_free(r);
```

| Code | Constant | Meaning |
|------|---------|---------|
| 0 | `OUCS_OK` | Success |
| -2 | `OUCS_ERR_IO` | File I/O error |
| -3 | `OUCS_ERR_INVALID_MAGIC` | Not a `.oucs` file |
| -5 | `OUCS_ERR_CORRUPT` | File corrupted |
| -6 | `OUCS_ERR_NOT_FOUND` | Song not found |
| -9 | `OUCS_ERR_WRONG_PASSWORD` | Wrong decryption password |
| -10 | `OUCS_ERR_ECC_FAIL` | Too many errors to correct |

---

## Hook System

```c
// Monitor every chunk during streaming
void my_chunk_hook(const uint8_t *chunk, size_t size, void *userdata) {
    printf("Chunk: %zu bytes\n", size);
}

OucsStream *s = oucs_stream_open(reader, 0, 4096, NULL);
oucs_stream_register_hook(s, OUCS_HOOK_CHUNK_READ, my_chunk_hook, NULL);

// Global hook — fires for ALL operations
oucs_hook_register_global(OUCS_HOOK_ON_ERROR, my_error_handler, NULL);
```

---

## Running Tests

```bash
# Build test binary
cc -std=c11 -I include \
  src/oucs_encoder.c src/oucs_decoder.c src/oucs_stream.c \
  src/oucs_ecc.c src/oucs_crypto.c src/oucs_analysis.c \
  src/oucs_delta.c src/oucs_history.c src/oucs_hooks.c \
  src/oucs_network.c src/oucs_util.c \
  tests/test_oucs.c -lm -o build/oucs_test

./build/oucs_test
# Results: 12 passed, 0 failed
```

Or with CMake:
```bash
cmake -B build -DOUCS_BUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

---

## Sample Apps

| App | Location | How to run |
|-----|----------|-----------|
| Browser Creator | `public_test/create.html` | `python3 -m http.server 8080` in `public_test/`, open http://localhost:8080/create.html |
| Browser Player | `public_test/player.html` | Same server, open http://localhost:8080/player.html |
| Web Player | `samples/web/index.html` | Open directly in browser — no server needed |
| Python Player | `samples/python/player.py` | `python3 samples/python/player.py play playlist.oucs 0` |
| Node.js Server | `samples/nodejs/server.js` | `node samples/nodejs/server.js playlist.oucs --port 3000` |

---

## Contributing

1. Fork the repository: **github.com/ParthHariom/oucs**
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make changes (C core first, then bindings)
4. Run tests: `./build/oucs_test`
5. Submit a pull request

Please keep PRs focused — one feature or fix per PR.

---

## License

MIT License — see [LICENSE](LICENSE) for full text.

---

## Roadmap

- [ ] v1.1 — libsodium backend (hardware AES acceleration)
- [ ] v1.2 — Chromaprint integration (production fingerprinting)
- [ ] v1.3 — Swift bindings (iOS/macOS native)
- [ ] v1.4 — Rust bindings
- [ ] v1.5 — Desktop music player app (Electron/Python)
- [ ] v2.0 — Streaming delta compression

---

*OUCS Engine — Built for developers, designed for music.*
