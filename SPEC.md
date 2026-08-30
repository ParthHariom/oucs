# OUCS Format Specification — Version 1.0

> **Open Universal Container for Sound**
> Binary format specification for `.oucs` files.

---

## 1. Overview

The `.oucs` file format is a binary container for multiple audio files. It supports:
- Selective unit-by-unit streaming (only the target song is loaded)
- Embedded per-song metadata, lyrics, waveform, fingerprint, chapters
- AES-256-GCM optional encryption (per-song or full container)
- Reed-Solomon error correction per audio unit
- Append-only version history
- HTTP Range request compatible layout
- Offline-first sync manifest

All multi-byte integers are stored in **little-endian** byte order unless noted.
All strings are **UTF-8** encoded and null-terminated.
All offsets are absolute byte positions from the start of the file (uint64).
A zero-value offset means the block is absent/not present.

---

## 2. Magic Bytes & Version

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| 0 | 4 | Magic | `0x4F 0x55 0x43 0x53` ("OUCS") |
| 4 | 1 | Major version | `0x01` |
| 5 | 1 | Minor version | `0x00` |

---

## 3. File Header (42 bytes fixed)

```
Offset  Size  Type    Field
------  ----  ------  -----
0       4     char[4] magic                  = "OUCS"
4       1     uint8   version_major          = 1
5       1     uint8   version_minor          = 0
6       1     uint8   flags                  (see Flags Register)
7       1     uint8   reserved               = 0x00
8       4     uint32  song_count
12      8     uint64  container_meta_offset
20      8     uint64  index_table_offset
28      8     uint64  sync_manifest_offset   (0 if absent)
36      8     uint64  version_history_offset (0 if absent)
```

**Total header size: 44 bytes**

### 3.1 Flags Register (1 byte)

| Bit | Meaning |
|-----|---------|
| 0   | `OUCS_FLAG_ENCRYPTED` — full container is encrypted |
| 1   | `OUCS_FLAG_HAS_SYNC`  — sync manifest block present |
| 2   | `OUCS_FLAG_HAS_HISTORY` — version history block present |
| 3   | `OUCS_FLAG_READONLY` — file should not be modified |
| 4-7 | Reserved, must be 0 |

---

## 4. Container Metadata Block

Located at `container_meta_offset`. Contains theme/description/logo for the whole container.

```
Offset  Size   Type     Field
------  -----  -------  -----
0       256    char[]   theme_name        (null-terminated, UTF-8)
256     1024   char[]   description       (null-terminated, UTF-8)
1280    4      uint32   logo_size         (bytes; 0 = no embedded logo)
1284    N      uint8[]  logo_bytes        (raw image bytes, any format)
1284+N  512    char[]   logo_ext_url      (null-terminated URL; empty if none)
...     8      uint64   created_at        (Unix timestamp, seconds)
...     32     uint8[]  author_uuid       (UUID v4, 16 bytes) + name (16 bytes)
...     4      uint32   crc32             (CRC-32 of all preceding bytes in this block)
```

---

## 5. Index Table

Located at `index_table_offset`. Contains one entry per song. Each entry is **fixed size: 512 bytes**.

```
Offset  Size  Type     Field
------  ----  -------  -----
0       16    uint8[]  uuid              (UUID v4 bytes)
16      64    char[]   name              (null-padded, UTF-8)
80      256   char[]   description       (null-padded, UTF-8)
336     8     uint64   byte_offset       (absolute offset of audio unit in file)
344     8     uint64   byte_size         (size of audio data in bytes)
352     4     char[4]  audio_format      ("MP3 ", "FLAC", "OGG ", "WAV ", "AAC ")
356     4     uint32   crc32             (CRC-32 of audio bytes)
360     8     uint64   ecc_offset        (offset of Reed-Solomon parity block; 0 if absent)
368     8     uint64   ecc_size          (size of ECC block in bytes)
376     8     uint64   waveform_offset   (offset of waveform data block; 0 if absent)
384     8     uint64   waveform_size     (number of float32 samples)
392     8     uint64   fingerprint_offset(offset of fingerprint block; 0 if absent)
400     4     uint32   fingerprint_size  (bytes)
404     8     uint64   lyrics_offset     (offset of lyrics block; 0 if absent)
412     4     uint32   lyrics_size       (bytes)
416     8     uint64   chapters_offset   (offset of chapters block; 0 if absent)
424     4     uint32   chapters_count
428     8     uint64   accessibility_offset (0 if absent)
436     4     float32  bpm               (0.0 if unknown)
440     1     uint8    musical_key       (0-11: C,C#,D,D#,E,F,F#,G,G#,A,A#,B; 255=unknown)
441     1     uint8    mood_flags        (bitmask: bit0=happy, bit1=sad, bit2=energetic, bit3=calm)
442     4     char[4]  language          (ISO 639-1 code, null-padded; e.g. "en\0\0")
446     16    uint8[]  delta_ref_uuid    (UUID of reference song for delta; all zeros if none)
462     1     uint8    encryption_flag   (0=none, 1=AES-256-GCM)
463     12    uint8[]  iv_nonce          (AES-GCM nonce; zeros if not encrypted)
475     4     uint32   duration_ms       (audio duration in milliseconds)
479     4     uint32   sample_rate       (Hz; e.g. 44100)
483     2     uint16   channels          (1=mono, 2=stereo)
485     2     uint16   bitrate_kbps      (e.g. 320)
487     1     uint8    track_number      (0 = unset)
488     24    uint8[]  reserved          (must be zero)
```

**Index entry size: 512 bytes**
**Index table size: song_count × 512 bytes**

---

## 6. Song Unit

Each song unit begins at `byte_offset` in the index entry. Layout:

```
[audio_bytes]          — raw original audio data (byte_size bytes)
[reed_solomon_parity]  — at ecc_offset, ecc_size bytes
[waveform_block]       — at waveform_offset (see §6.1)
[fingerprint_block]    — at fingerprint_offset (see §6.2)
[lyrics_block]         — at lyrics_offset (see §6.3)
[chapters_block]       — at chapters_offset (see §6.4)
[accessibility_block]  — at accessibility_offset (see §6.5)
```

### 6.1 Waveform Block
Array of float32 values. Each value = RMS amplitude of a 1024-sample window.
Count = `waveform_size` (number of float32 values).

### 6.2 Fingerprint Block
Raw Chromaprint fingerprint bytes. Size = `fingerprint_size`.

### 6.3 Lyrics Block (LRC Binary)
```
Offset  Size  Type    Field
0       4     uint32  entry_count
per entry:
  0     4     uint32  timestamp_ms
  4     2     uint16  text_length
  6     N     char[]  text (UTF-8, NOT null-terminated)
```

### 6.4 Chapters Block
```
Offset  Size  Type    Field
0       4     uint32  chapter_count
per entry:
  0     4     uint32  offset_ms       (millisecond position)
  4     2     uint16  name_length
  6     N     char[]  name (UTF-8)
```

### 6.5 Accessibility Block
```
Offset  Size   Type    Field
0       4      char[4] language (ISO 639-1)
4       4      uint32  description_length
8       N      char[]  audio_description (UTF-8)
8+N     4      uint32  transcript_length
12+N    M      char[]  transcript (UTF-8)
```

---

## 7. Reed-Solomon Error Correction

Each song unit has an optional ECC block at `ecc_offset`.
- Encoding: RS(255, 223) — 32 parity bytes per 223 data bytes (BCH compatible)
- The audio bytes are split into 223-byte blocks; each gets 32 parity bytes
- ECC block layout: sequential parity chunks, one per data block
- Recovery: up to 16 byte-errors per 255-byte block can be corrected

---

## 8. Sync Manifest Block

Located at `sync_manifest_offset` (0 if `OUCS_FLAG_HAS_SYNC` is not set).

```
Offset  Size  Type    Field
0       4     uint32  device_count
per device (32 bytes each):
  0     16    uint8[] device_id (UUID v4)
  16    8     uint64  last_sync_ts (Unix timestamp)
  24    8     uint8[] reserved
after devices:
  0     4     uint32  changelog_entry_count
per entry (25 bytes each):
  0     8     uint64  timestamp
  1     1     uint8   operation (0=ADD, 1=REMOVE, 2=UPDATE)
  9     16    uint8[] song_uuid
```

---

## 9. Version History Block

Located at `version_history_offset` (0 if `OUCS_FLAG_HAS_HISTORY` is not set).

```
Offset  Size  Type    Field
0       4     uint32  entry_count
per entry:
  0     8     uint64  timestamp
  4     1     uint8   operation (0=CREATE, 1=ADD, 2=REMOVE, 3=UPDATE, 4=MERGE)
  9     16    uint8[] song_uuid (all zeros for container-level ops)
  25    64    char[]  note (null-terminated, optional human-readable description)
```

---

## 10. Encryption

When `encryption_flag = 1` in an index entry:
- The audio bytes for that song are encrypted with AES-256-GCM
- The `iv_nonce` (12 bytes) is stored in the index entry
- The encryption key is derived via PBKDF2-SHA256:
  - Salt: first 16 bytes of container UUID (from container metadata)
  - Iterations: 100,000
  - Key length: 32 bytes
- The GCM authentication tag (16 bytes) is appended after the ciphertext
- Encrypted `byte_size` includes the 16-byte auth tag

When `OUCS_FLAG_ENCRYPTED` is set in the file header:
- The entire container metadata block is also encrypted
- A global IV is stored at bytes 44-55 (12 bytes, after the header)

---

## 11. Alignment & Padding

- The file header is always 44 bytes
- Song units SHOULD be aligned to 512-byte boundaries for efficient disk I/O
- Padding bytes (value 0x00) may be inserted between blocks
- Implementations MUST use offsets from the index table (never assume sequential layout)

---

## 12. MIME Type & File Extension

- File extension: `.oucs`
- MIME type: `application/x-oucs`
- Magic bytes: `4F 55 43 53` at offset 0

---

## 13. Versioning

- This document describes OUCS format version **1.0** (`version_major=1, version_minor=0`)
- Future minor versions add new optional fields (backward compatible)
- Future major versions may break backward compatibility
- Implementations MUST reject files with `version_major > 1`
- Implementations SHOULD warn on unknown `version_minor`

---

*OUCS Format Specification v1.0 — MIT License*
