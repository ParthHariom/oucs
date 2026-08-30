# OUCS Engine — Complete Implementation Plan
> **Open Universal Container for Sound**
> *"Pack once, stream anything, everywhere."*

---

## Project Overview

OUCS ek binary container format (`.oucs`) hai jo multiple audio files ko ek smart file me pack karta hai. Ye ZIP nahi hai — ye ek **streaming-first, corruption-resistant, encrypted, metadata-rich audio container engine** hai jiska C core sabse pehle build hoga aur phir Python, JavaScript, Java me bindings honge. Koi bhi developer iske upar desktop, mobile, web, iOS app bana sake.

**License:** MIT  
**Core Language:** C (liboucs)  
**Bindings:** Python, JavaScript (WASM + Node.js), Java (JNI/Android)  
**File Extension:** `.oucs`

---

## Problem Statement

Existing audio container solutions have critical limitations:

| Solution | Problem |
|----------|---------|
| ZIP/TAR | Poora extract karna padta hai |
| CUE sheets | Alag alag files chahiye |
| HLS/DASH | Server infrastructure chahiye |
| Podcast RSS | Metadata only, no container |
| **OUCS** | ✅ Single file, selective stream, encrypted, self-describing, works offline + online, any language, any platform |

---

## Success Probability Analysis

| Area | Reality Check | Chance |
|------|--------------|--------|
| Core C engine + `.oucs` format | Standard binary I/O + open source libs available | **95%** |
| Reed-Solomon error correction | `zfec` / `schifra` proven libraries hain | **92%** |
| Selective streaming (low memory) | HTTP Range + file seek — well understood | **95%** |
| AES-256 encryption | libsodium / OpenSSL mature hain | **93%** |
| Python/JS/Java bindings | CFFI/WASM/JNI — documented paths | **88%** |
| Waveform pre-computation | `minimp3`/`dr_libs` open source available | **90%** |
| BPM/Key/Mood detection | Algorithms exist, accuracy ±5% acceptable | **80%** |
| Acoustic fingerprinting | Chromaprint open source available | **85%** |
| Delta packing | Complex but doable for exact duplicates | **75%** |
| HTTP range streaming | Standard HTTP — very achievable | **93%** |
| Lyrics/Chapters embed | Simple binary struct — straightforward | **97%** |
| Plugin/Hook system | Function pointers in C — simple | **95%** |
| Sync manifest | Basic append-only log — doable | **85%** |
| **Overall project success** | **Agar step-by-step karo, core se shuru karo** | **87%** |

> **Honest note:** Ye project fail tab hoga jab sab kuch ek saath karne ki koshish ho. Agar Task 1 se shuru karo aur har task demo karo — success almost guaranteed hai. Sabse risky part BPM/mood detection aur delta packing hai — ye optional features hain, core pe koi risk nahi.

---

## Full Feature List

### Container Format
- `.oucs` binary format, magic bytes, versioned
- Fixed-order playlist, skip (next/prev) support
- Append-only version history
- Merge & split without re-encoding
- Checksum-based deduplication
- Delta/diff packing for similar songs

### Streaming & Performance
- Chunk-by-chunk selective streaming (unit-by-unit)
- HTTP Range request support (CDN-friendly)
- File seek-based extraction (zero full-load)
- Configurable buffer size
- Device load near zero — only target song in memory

### Security & Reliability
- AES-256-GCM encryption (per-song or full container)
- Reed-Solomon error correction per unit
- CRC-32 checksum per unit
- Metadata redundancy (backup copies)
- PBKDF2 key derivation
- Corruption chances near zero

### Metadata & Intelligence
- Container: theme name, description, embedded logo + external URL
- Per-song: UUID, name, description, language tag
- Synced lyrics (LRC format, time-stamped)
- Chapter markers + millisecond seek
- Waveform preview data (pre-computed at pack time)
- BPM, musical key, mood tags (auto-detected at pack time)
- Acoustic fingerprint (Chromaprint-based)
- Accessibility: audio description, hearing-impaired transcript

### Developer Features
- Plugin/hook system (`on_pack`, `on_chunk_read`, `on_error`, `on_extract`)
- Offline-first sync manifest block
- Full API for all operations
- Trigger-based file info (metadata on file open/query)
- Proper music/playlist vibe — metadata makes intent clear

### Multi-language Support
- C core (`liboucs`) — the engine
- Python → CFFI (`pip install oucs`)
- JavaScript → WASM + N-API (`npm install oucs`)
- Java → JNI (Maven/Gradle, Android ready)

### CLI Tooling
- `oucs-pack` — files se `.oucs` banao
- `oucs-extract` — selective extract
- `oucs-info` — metadata dump
- `oucs-merge` — playlists combine
- `oucs-split` — playlist split
- `oucs-dedup` — duplicates find/remove
- `oucs-history` — version log dekho

---

## `.oucs` Binary File Structure (Core Level)

```
BYTE 0-3   : Magic "OUCS"
BYTE 4     : Version
BYTE 5     : Flags (encryption, compression, sync)
BYTE 6-9   : Song count (uint32)
BYTE 10-17 : Container metadata block offset (uint64)
BYTE 18-25 : Index table offset (uint64)
BYTE 26-33 : Sync manifest offset (uint64)
BYTE 34-41 : Version history offset (uint64)
─────────────────────────────────────────────────────
[CONTAINER METADATA BLOCK]
  theme_name     (null-terminated string, max 256)
  description    (null-terminated string, max 1024)
  logo_size      (4 bytes, uint32) + logo_bytes
  logo_ext_url   (null-terminated string, max 512)
  created_at     (8 bytes, unix timestamp uint64)
  crc32          (4 bytes, checksum of this block)
─────────────────────────────────────────────────────
[INDEX TABLE — per song entry]
  uuid               (16 bytes)
  name               (64 bytes, null-padded)
  description        (256 bytes, null-padded)
  byte_offset        (8 bytes, uint64)
  byte_size          (8 bytes, uint64)
  audio_format       (4 bytes: mp3/flac/ogg/wav/aac)
  crc32              (4 bytes)
  ecc_offset         (8 bytes, uint64)
  waveform_offset    (8 bytes, uint64)
  fingerprint_offset (8 bytes, uint64)
  lyrics_offset      (8 bytes, uint64)
  chapters_offset    (8 bytes, uint64)
  bpm                (4 bytes, float32)
  musical_key        (1 byte, 0-11 chromatic scale)
  mood_tag           (4 bytes, enum flags)
  language           (4 bytes, ISO 639 code)
  accessibility_offset (8 bytes, uint64)
  delta_ref_uuid     (16 bytes, zero if none)
  encryption_flag    (1 byte)
  iv_nonce           (12 bytes, AES-GCM nonce)
─────────────────────────────────────────────────────
[SONG UNIT 0..N]
  audio_bytes          (raw original mp3/flac/etc)
  reed_solomon_parity  (ECC recovery data)
  lyrics_block         (LRC binary, time-stamped)
  waveform_block       (float32 array, RMS per chunk)
  fingerprint_block    (Chromaprint hash bytes)
  chapters_block       (name + ms offset pairs)
  accessibility_block  (language, description, transcript)
─────────────────────────────────────────────────────
[SYNC MANIFEST]
  device_count   (4 bytes, uint32)
  per device:
    device_id    (16 bytes UUID)
    last_sync_ts (8 bytes uint64)
  change_log entries (append-only):
    timestamp    (8 bytes)
    operation    (1 byte: ADD/REMOVE/UPDATE)
    song_uuid    (16 bytes)
─────────────────────────────────────────────────────
[VERSION HISTORY]
  entry_count (4 bytes, uint32)
  per entry:
    timestamp   (8 bytes)
    operation   (null-terminated string)
    song_uuid   (16 bytes)
```

---

## Multi-language Architecture

```
C Core (liboucs)
    │
    ├── Python  ──→ CFFI wrapper        ──→ pip install oucs
    │
    ├── JS      ──→ Emscripten (WASM)   ──→ npm install oucs
    │               + Node.js N-API
    │
    └── Java    ──→ JNI wrapper         ──→ Maven / Gradle
                    (Android + Desktop)
```

---

## 24-Task Roadmap

### Task 1: `.oucs` File Format Specification
- **Objective:** Complete binary format ka spec document likho
- **Implementation:** `SPEC.md` — har block ka exact byte layout, field sizes, endianness, magic bytes, version scheme, flags register define karo
- **Test:** Spec review checklist — koi ambiguity na ho, har field ka size fixed ho
- **Demo:** `SPEC.md` publicly readable, koi bhi developer isko padhke independent implementation likh sake

---

### Task 2: C Core — Basic Encoder (Writer)
- **Objective:** Songs pack karke `.oucs` file banana
- **Implementation:** `oucs_create()`, `oucs_add_song()`, `oucs_set_container_meta()`, `oucs_finalize()`. CRC-32 per song. Index table build karo. Binary file write karo.
- **Test:** Known input se `.oucs` file banao, hex dump se verify karo ki structure spec ke according hai
- **Demo:** `oucs-pack playlist.oucs song1.mp3 song2.mp3` — working `.oucs` file banta hai

---

### Task 3: C Core — Selective Extractor (Reader)
- **Objective:** Index table se specific song ko byte offset pe seek karke extract karo
- **Implementation:** `oucs_open()`, `oucs_get_song_info()`, `oucs_extract_song()`, `oucs_close()`. File seek karo, sirf target song ke bytes read karo.
- **Test:** Song 2 extract karo bina song 1 read kiye — verify karo
- **Demo:** `oucs-extract playlist.oucs 1 out.mp3` — sirf wahi song nikalta hai

---

### Task 4: Chunk-by-chunk Streaming API
- **Objective:** Low-memory streaming — poora song ek baar load nahi hoga
- **Implementation:** `oucs_stream_open()`, `oucs_stream_read_chunk()`, `oucs_stream_seek_chunk()`, `oucs_stream_close()`. Configurable buffer size.
- **Test:** 10MB song ko 4KB chunks me stream karo — peak memory < 1MB verify karo
- **Demo:** CLI mock player — chunks stream hote hain, skip karo, memory usage dikhao

---

### Task 5: Reed-Solomon Error Correction + Corruption Resistance
- **Objective:** Corrupt file bhi recover ho sake
- **Implementation:** Per-song RS parity blocks. Read time pe auto-correct. `zfec` ya custom RS library integrate karo. Metadata block bhi redundant copy rakhe.
- **Test:** Bytes manually flip karo → file still reads correctly
- **Demo:** Corrupted `.oucs` play karo — recovery automatic ho

---

### Task 6: Container + Per-Song Metadata
- **Objective:** Theme, logo, per-song naam/UUID/description full support
- **Implementation:** Metadata block binary encode karo. Logo embedded bytes + optional URL. `oucs_get_meta()`, `oucs_set_meta()`, `oucs_get_song_meta()` API.
- **Test:** Set karo → write karo → read karo → all fields exact match
- **Demo:** `oucs-info playlist.oucs` — container info + song list + logo size print ho

---

### Task 7: AES-256 Encryption
- **Objective:** Optional per-song ya full container encryption
- **Implementation:** libsodium/OpenSSL se AES-256-GCM. PBKDF2 key derivation. Encryption flag in header. `oucs_set_encryption()`, `oucs_unlock()` API.
- **Test:** Encrypted file bina password open na ho. Wrong password = clear error.
- **Demo:** `oucs-pack --encrypt playlist.oucs songs/` → bina password play nahi hota

---

### Task 8: Waveform Pre-computation
- **Objective:** Pack time pe waveform data embed karo — app me instant visual without decoding
- **Implementation:** Audio decode karo (minimp3/dr_libs), RMS per chunk calculate karo, compact float32 array store karo index me.
- **Test:** Waveform data extract karo → plot karo → visually correct dikhna chahiye
- **Demo:** Python script waveform data nikale aur terminal me ASCII graph dikhaye

---

### Task 9: Lyrics Embedding (LRC Synced)
- **Objective:** Time-synced lyrics per song store karo
- **Implementation:** LRC format parse karo, binary me efficient store karo. `oucs_get_lyrics()`, `oucs_get_lyric_at_time(timestamp)` API.
- **Test:** LRC file embed karo → extract karo → timestamps exact match
- **Demo:** Python script song stream kare aur current timestamp pe correct lyrics print kare

---

### Task 10: Chapters / Timestamps
- **Objective:** Song ke andar seek points define karo
- **Implementation:** Chapter struct: name + millisecond offset. Per-song chapter list in index. `oucs_get_chapters()`, `oucs_stream_seek_chapter()` API.
- **Test:** 3 chapters define karo → chapter 2 pe seek karo → correct byte position
- **Demo:** `oucs-chapters playlist.oucs 0` — song 0 ke chapters list ho, chapter pe jump karo

---

### Task 11: BPM / Key / Mood Auto-detection
- **Objective:** Pack time pe automatically audio analysis karke tags embed karo
- **Implementation:** BPM detection (autocorrelation algorithm), musical key (FFT-based chromagram), mood tags (energy/valence heuristic). Embed in index table.
- **Test:** Known BPM song → detected BPM ±2 range me ho
- **Demo:** `oucs-info` me BPM, key, mood automatically dikhein — no manual input needed

---

### Task 12: Acoustic Fingerprinting
- **Objective:** Duplicate song detection, song identity verification
- **Implementation:** Chromaprint algorithm (open source) integrate karo. Embed fingerprint in index. `oucs_find_duplicate()`, `oucs_match_fingerprint()` API.
- **Test:** Same song add karo → duplicate detected. Different song → no false positive.
- **Demo:** `oucs-dedup playlist.oucs` — duplicate songs list karo

---

### Task 13: Delta Packing + Deduplication
- **Objective:** Same/similar songs ek baar store ho, file size significantly kam ho
- **Implementation:** Fingerprint comparison se duplicates detect karo. Same song = one copy + reference pointer. Delta encoding for near-duplicate audio blocks.
- **Test:** Same song 3 baar add karo → file size = ~1x not 3x
- **Demo:** Before/after file size comparison dikhao with stats

---

### Task 14: Append-only Version History
- **Objective:** Songs add karo bina full file rewrite, history maintain karo
- **Implementation:** Append-only log at end of file. `oucs_append_song()`. Version entries in history block. `oucs_get_history()` API.
- **Test:** Song append karo → file valid rahe → history entry exist kare → old songs intact rahe
- **Demo:** `oucs-history playlist.oucs` — full changelog with timestamps dikhao

---

### Task 15: Plugin / Hook System
- **Objective:** Developers custom processing add kar sakein without modifying core
- **Implementation:** Function pointer hooks: `on_before_pack`, `on_chunk_read`, `on_after_extract`, `on_error`. Plugin registration API. `oucs_register_hook()`, `oucs_unregister_hook()`.
- **Test:** Hook register karo, song stream karo → hook calls verify karo with correct data
- **Demo:** Sample plugin jo har chunk ka volume level print kare — live output during streaming

---

### Task 16: `.oucs` Merge & Split CLI Tools
- **Objective:** Multiple `.oucs` files merge karo ya split karo bina re-encoding
- **Implementation:** `oucs-merge out.oucs a.oucs b.oucs` — index tables combine, song bytes append. `oucs-split playlist.oucs 0 3` — songs 0-3 naya file me nikalo.
- **Test:** Merge karo → split karo → original files byte-level match karein
- **Demo:** 2 playlists merge karo, phir split karo — integrity checksum verify ho

---

### Task 17: HTTP Range Request / Network Streaming
- **Objective:** Server pe rakhi `.oucs` file bina full download ke stream ho
- **Implementation:** `oucs_open_url()` — HTTP Range header se sirf needed bytes fetch karo. Index table pehle fetch, phir specific song bytes on demand. CDN-compatible design.
- **Test:** Local HTTP server pe file rakho → range requests verify karo → sirf needed bytes download hon
- **Demo:** Browser me `.oucs` URL dalo → songs list dikhe → play karo, full file download nahi hota (network tab proof)

---

### Task 18: Offline-first Sync Manifest
- **Objective:** Multiple devices pe playlist sync ho sake — no cloud dependency
- **Implementation:** Sync manifest block: device ID (UUID), timestamps, change log entries. `oucs_sync_manifest_get()`, `oucs_sync_manifest_merge()`. Developer apna transport layer use kare.
- **Test:** 2 devices ke manifests merge karo → conflicts resolve hon → unified state correct ho
- **Demo:** 2 `.oucs` files ka manifest merge karo → unified change log print ho

---

### Task 19: Accessibility Metadata
- **Objective:** Audio descriptions, language tags, transcripts embed karo — accessible apps possible
- **Implementation:** Per-song accessibility struct: language code (ISO 639), audio description text, hearing-impaired transcript. `oucs_get_accessibility()`, `oucs_set_accessibility()` API.
- **Test:** Set karo → write karo → read karo → all fields exact match
- **Demo:** `oucs-info` me language aur accessibility info clearly dikhe

---

### Task 20: Python Bindings
- **Objective:** Pythonic API, `pip install oucs`
- **Implementation:** CFFI wrapper over liboucs. `OucsFile`, `OucsSong`, `OucsStream` Python classes. Context managers (`with` statement). Async streaming support (asyncio compatible).
- **Test:** Full unit test suite — pack, stream, metadata, encryption, hooks, lyrics, waveform
- **Demo:** Python script — 3 songs pack karo, metadata set karo, song 2 chunk-by-chunk stream karo

---

### Task 21: JavaScript / WebAssembly Bindings
- **Objective:** Browser aur Node.js dono me kaam kare seamlessly
- **Implementation:** Emscripten se C code ko WASM compile karo. Node.js N-API wrapper bhi. `npm install oucs`. Async ReadableStream API. MediaSource API integration example.
- **Test:** Node.js unit tests. Browser test page with `<audio>` element fed via MediaSource.
- **Demo:** HTML page — `.oucs` file load, songs list dikhe, play/next/prev kaam kare, waveform visual dikhao, lyrics sync ho

---

### Task 22: Java Bindings
- **Objective:** Android + Desktop Java both supported
- **Implementation:** JNI wrapper over liboucs. Maven/Gradle package. `OucsEngine`, `OucsStream`, `OucsMeta` Java classes. Android `jniLibs` prebuilt `.so` files setup.
- **Test:** JUnit tests — pack, stream, metadata, encryption, waveform, lyrics
- **Demo:** Android app — songs list, play/skip, waveform bar, lyrics sync live. Desktop Java player bhi same library se.

---

### Task 23: Cross-platform Build System + CI/CD
- **Objective:** Ek command se sab platforms build ho, automated releases on GitHub
- **Implementation:** CMake for C core. GitHub Actions: Windows/Mac/Linux/Android/WASM/iOS matrix builds. Release artifacts: `.so`, `.dll`, `.dylib`, `.wasm`, Python wheel, npm package, Maven jar.
- **Test:** Clean environment me build karo — sab bindings compile ho aur tests pass karein
- **Demo:** GitHub Releases page pe sab packages available. `pip install oucs`, `npm install oucs`, Maven dependency — teeno live aur working.

---

### Task 24: Developer Documentation + Sample Apps
- **Objective:** Koi bhi developer library utha ke app bana sake — zero friction
- **Implementation:** `README.md` with quickstart. Full API reference (Doxygen for C, JSDoc for JS, Javadoc for Java, Sphinx for Python). 5 sample apps: Python CLI player, JS web player, Java Android player, C CLI tool, Node.js streaming server.
- **Test:** Ek new developer docs padhke app banaye — time to first working app < 30 minutes
- **Demo:** Web player browser me chal raha hai, Python player terminal me, Android app phone pe — sab `.oucs` se live stream kar rahe hain

---

## Quick Roadmap Summary

```
Task 1  → SPEC.md — complete format specification
Task 2  → C Encoder — pack songs into .oucs
Task 3  → C Decoder — selective extract by index/UUID
Task 4  → Streaming API — chunk-by-chunk, low memory
Task 5  → Error Correction — Reed-Solomon + CRC
Task 6  → Metadata — container + per-song full support
Task 7  → Encryption — AES-256-GCM, PBKDF2
Task 8  → Waveform — pre-compute at pack time
Task 9  → Lyrics — LRC embed + time-seek API
Task 10 → Chapters — timestamp markers + seek
Task 11 → BPM/Key/Mood — auto-detect at pack time
Task 12 → Fingerprinting — Chromaprint, dedup detect
Task 13 → Delta Packing — deduplication + diff store
Task 14 → Version History — append-only changelog
Task 15 → Plugin/Hook System — developer extensibility
Task 16 → Merge & Split — CLI tools, no re-encoding
Task 17 → HTTP Range Streaming — CDN-friendly network
Task 18 → Sync Manifest — offline-first multi-device
Task 19 → Accessibility — lang tags, transcripts
Task 20 → Python Bindings — pip install oucs
Task 21 → JS/WASM Bindings — npm install oucs
Task 22 → Java Bindings — Maven/Gradle/Android
Task 23 → Build System + CI/CD — all platforms auto
Task 24 → Docs + Sample Apps — 5 reference apps
```

---

## Overall Success Assessment

**87% success probability** — agar core-first approach lo (Task 1→5 pehle solid karo), baaki sab uske upar build hoga. Ye project ek developer bhi complete kar sakta hai 6-8 mahine me. Open source community mile to 3-4 mahine.

**Biggest risk:** Scope creep — ek saath sab mat karo. Core engine aur streaming pehle. Baaki features incrementally add karo.

**Most unique aspect:** Kisi bhi existing audio container me selective unit-by-unit streaming + embedded waveform + auto BPM/mood + encryption + HTTP range — ye combination abhi kisi ke paas nahi hai.

---

*OUCS Engine — Built for developers, designed for music.*
