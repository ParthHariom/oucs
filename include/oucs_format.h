/**
 * OUCS Engine - Core Format Header
 * Open Universal Container for Sound
 *
 * This header defines all structs, constants, enums, and API declarations
 * for the liboucs C core engine.
 *
 * License: MIT
 * Version: 1.0.0
 */

#ifndef OUCS_FORMAT_H
#define OUCS_FORMAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─────────────────────────────────────────────────────────────
   VERSION
───────────────────────────────────────────────────────────── */

#define OUCS_VERSION_MAJOR    1
#define OUCS_VERSION_MINOR    0
#define OUCS_VERSION_PATCH    0
#define OUCS_VERSION_STRING   "1.0.0"

/* ─────────────────────────────────────────────────────────────
   MAGIC & SIZES
───────────────────────────────────────────────────────────── */

#define OUCS_MAGIC            "OUCS"
#define OUCS_MAGIC_SIZE       4
#define OUCS_HEADER_SIZE      44
#define OUCS_INDEX_ENTRY_SIZE 512
#define OUCS_META_NAME_MAX    256
#define OUCS_META_DESC_MAX    1024
#define OUCS_META_URL_MAX     512
#define OUCS_SONG_NAME_MAX    64
#define OUCS_SONG_DESC_MAX    256
#define OUCS_CHUNK_SIZE_DEFAULT 4096
#define OUCS_UUID_SIZE        16

/* ─────────────────────────────────────────────────────────────
   FLAGS
───────────────────────────────────────────────────────────── */

#define OUCS_FLAG_ENCRYPTED    (1 << 0)
#define OUCS_FLAG_HAS_SYNC     (1 << 1)
#define OUCS_FLAG_HAS_HISTORY  (1 << 2)
#define OUCS_FLAG_READONLY     (1 << 3)

/* ─────────────────────────────────────────────────────────────
   AUDIO FORMAT CODES
───────────────────────────────────────────────────────────── */

#define OUCS_FMT_MP3   "MP3 "
#define OUCS_FMT_FLAC  "FLAC"
#define OUCS_FMT_OGG   "OGG "
#define OUCS_FMT_WAV   "WAV "
#define OUCS_FMT_AAC   "AAC "
#define OUCS_FMT_OPUS  "OPUS"

/* ─────────────────────────────────────────────────────────────
   MUSICAL KEY CONSTANTS
───────────────────────────────────────────────────────────── */

typedef enum {
    OUCS_KEY_C   = 0,
    OUCS_KEY_Cs  = 1,
    OUCS_KEY_D   = 2,
    OUCS_KEY_Ds  = 3,
    OUCS_KEY_E   = 4,
    OUCS_KEY_F   = 5,
    OUCS_KEY_Fs  = 6,
    OUCS_KEY_G   = 7,
    OUCS_KEY_Gs  = 8,
    OUCS_KEY_A   = 9,
    OUCS_KEY_As  = 10,
    OUCS_KEY_B   = 11,
    OUCS_KEY_UNKNOWN = 255
} OucsMusicalKey;

/* ─────────────────────────────────────────────────────────────
   MOOD FLAGS
───────────────────────────────────────────────────────────── */

#define OUCS_MOOD_HAPPY     (1 << 0)
#define OUCS_MOOD_SAD       (1 << 1)
#define OUCS_MOOD_ENERGETIC (1 << 2)
#define OUCS_MOOD_CALM      (1 << 3)
#define OUCS_MOOD_ROMANTIC  (1 << 4)
#define OUCS_MOOD_ANGRY     (1 << 5)

/* ─────────────────────────────────────────────────────────────
   ENCRYPTION FLAGS
───────────────────────────────────────────────────────────── */

#define OUCS_ENC_NONE       0
#define OUCS_ENC_AES256GCM  1

/* ─────────────────────────────────────────────────────────────
   SYNC MANIFEST OPERATIONS
───────────────────────────────────────────────────────────── */

typedef enum {
    OUCS_SYNC_ADD    = 0,
    OUCS_SYNC_REMOVE = 1,
    OUCS_SYNC_UPDATE = 2
} OucsSyncOp;

/* ─────────────────────────────────────────────────────────────
   VERSION HISTORY OPERATIONS
───────────────────────────────────────────────────────────── */

typedef enum {
    OUCS_HIST_CREATE = 0,
    OUCS_HIST_ADD    = 1,
    OUCS_HIST_REMOVE = 2,
    OUCS_HIST_UPDATE = 3,
    OUCS_HIST_MERGE  = 4
} OucsHistOp;

/* ─────────────────────────────────────────────────────────────
   ERROR CODES
───────────────────────────────────────────────────────────── */

typedef enum {
    OUCS_OK                  =  0,
    OUCS_ERR_NULL_PARAM      = -1,
    OUCS_ERR_IO              = -2,
    OUCS_ERR_INVALID_MAGIC   = -3,
    OUCS_ERR_VERSION         = -4,
    OUCS_ERR_CORRUPT         = -5,
    OUCS_ERR_NOT_FOUND       = -6,
    OUCS_ERR_NOMEM           = -7,
    OUCS_ERR_CRYPTO          = -8,
    OUCS_ERR_WRONG_PASSWORD  = -9,
    OUCS_ERR_ECC_FAIL        = -10,
    OUCS_ERR_INVALID_ARG     = -11,
    OUCS_ERR_OVERFLOW        = -12,
    OUCS_ERR_ALREADY_EXISTS  = -13,
    OUCS_ERR_NETWORK         = -14,
    OUCS_ERR_UNSUPPORTED     = -15
} OucsError;

/* ─────────────────────────────────────────────────────────────
   UUID TYPE
───────────────────────────────────────────────────────────── */

typedef uint8_t OucsUUID[OUCS_UUID_SIZE];

/* ─────────────────────────────────────────────────────────────
   FILE HEADER STRUCT
───────────────────────────────────────────────────────────── */

#pragma pack(push, 1)

typedef struct {
    char     magic[4];                  /* "OUCS" */
    uint8_t  version_major;             /* 1 */
    uint8_t  version_minor;             /* 0 */
    uint8_t  flags;                     /* OUCS_FLAG_* bitmask */
    uint8_t  reserved;                  /* must be 0 */
    uint32_t song_count;
    uint64_t container_meta_offset;
    uint64_t index_table_offset;
    uint64_t sync_manifest_offset;      /* 0 if absent */
    uint64_t version_history_offset;    /* 0 if absent */
} OucsFileHeader;

/* ─────────────────────────────────────────────────────────────
   INDEX ENTRY STRUCT (512 bytes fixed)
───────────────────────────────────────────────────────────── */

typedef struct {
    OucsUUID uuid;                      /* 16 bytes */
    char     name[64];                  /* null-padded UTF-8 */
    char     description[256];          /* null-padded UTF-8 */
    uint64_t byte_offset;               /* absolute file offset of audio data */
    uint64_t byte_size;                 /* size of audio data in bytes */
    char     audio_format[4];           /* "MP3 ", "FLAC", etc. */
    uint32_t crc32;                     /* CRC-32 of audio bytes */
    uint64_t ecc_offset;                /* 0 if absent */
    uint64_t ecc_size;
    uint64_t waveform_offset;           /* 0 if absent */
    uint64_t waveform_size;             /* number of float32 samples */
    uint64_t fingerprint_offset;        /* 0 if absent */
    uint32_t fingerprint_size;          /* bytes */
    uint64_t lyrics_offset;             /* 0 if absent */
    uint32_t lyrics_size;
    uint64_t chapters_offset;           /* 0 if absent */
    uint32_t chapters_count;
    uint64_t accessibility_offset;      /* 0 if absent */
    float    bpm;                       /* 0.0 if unknown */
    uint8_t  musical_key;               /* OucsMusicalKey */
    uint8_t  mood_flags;                /* OUCS_MOOD_* bitmask */
    char     language[4];               /* ISO 639-1, null-padded */
    OucsUUID delta_ref_uuid;            /* all zeros if no delta */
    uint8_t  encryption_flag;           /* OUCS_ENC_* */
    uint8_t  iv_nonce[12];              /* AES-GCM nonce */
    uint32_t duration_ms;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bitrate_kbps;
    uint8_t  track_number;
    uint8_t  reserved[24];             /* must be zero */
} OucsIndexEntry;

/* ─────────────────────────────────────────────────────────────
   CONTAINER METADATA (in-memory, not packed)
───────────────────────────────────────────────────────────── */

#pragma pack(pop)

typedef struct {
    char     theme_name[OUCS_META_NAME_MAX];
    char     description[OUCS_META_DESC_MAX];
    uint8_t *logo_bytes;                /* heap-allocated; NULL if none */
    uint32_t logo_size;
    char     logo_ext_url[OUCS_META_URL_MAX];
    uint64_t created_at;                /* Unix timestamp */
    OucsUUID author_uuid;
    char     author_name[16];
} OucsContainerMeta;

/* ─────────────────────────────────────────────────────────────
   LYRICS ENTRY
───────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t timestamp_ms;
    char    *text;                      /* heap-allocated UTF-8 */
} OucsLyricEntry;

typedef struct {
    uint32_t       count;
    OucsLyricEntry *entries;            /* heap-allocated array */
} OucsLyrics;

/* ─────────────────────────────────────────────────────────────
   CHAPTER
───────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t offset_ms;
    char    *name;                      /* heap-allocated */
} OucsChapter;

typedef struct {
    uint32_t    count;
    OucsChapter *entries;
} OucsChapters;

/* ─────────────────────────────────────────────────────────────
   ACCESSIBILITY
───────────────────────────────────────────────────────────── */

typedef struct {
    char  language[4];
    char *audio_description;            /* heap-allocated */
    char *transcript;                   /* heap-allocated */
} OucsAccessibility;

/* ─────────────────────────────────────────────────────────────
   WAVEFORM
───────────────────────────────────────────────────────────── */

typedef struct {
    float   *samples;                   /* heap-allocated float32 array */
    uint64_t count;                     /* number of samples */
} OucsWaveform;

/* ─────────────────────────────────────────────────────────────
   FINGERPRINT
───────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t *data;                      /* heap-allocated */
    uint32_t size;
} OucsFingerprint;

/* ─────────────────────────────────────────────────────────────
   SYNC MANIFEST
───────────────────────────────────────────────────────────── */

typedef struct {
    OucsUUID  device_id;
    uint64_t  last_sync_ts;
} OucsSyncDevice;

typedef struct {
    uint64_t  timestamp;
    uint8_t   operation;                /* OucsSyncOp */
    OucsUUID  song_uuid;
} OucsSyncEntry;

typedef struct {
    uint32_t      device_count;
    OucsSyncDevice *devices;
    uint32_t      entry_count;
    OucsSyncEntry *entries;
} OucsSyncManifest;

/* ─────────────────────────────────────────────────────────────
   VERSION HISTORY
───────────────────────────────────────────────────────────── */

typedef struct {
    uint64_t  timestamp;
    uint8_t   operation;                /* OucsHistOp */
    OucsUUID  song_uuid;
    char      note[64];
} OucsHistoryEntry;

typedef struct {
    uint32_t        entry_count;
    OucsHistoryEntry *entries;
} OucsHistory;

/* ─────────────────────────────────────────────────────────────
   HOOK SYSTEM
───────────────────────────────────────────────────────────── */

typedef enum {
    OUCS_HOOK_BEFORE_PACK    = 0,
    OUCS_HOOK_AFTER_PACK     = 1,
    OUCS_HOOK_CHUNK_READ     = 2,
    OUCS_HOOK_AFTER_EXTRACT  = 3,
    OUCS_HOOK_ON_ERROR       = 4,
    OUCS_HOOK_COUNT          = 5
} OucsHookType;

/* Hook callback signatures */
typedef void (*OucsHookBeforePack)   (const char *song_path, void *userdata);
typedef void (*OucsHookAfterPack)    (const OucsIndexEntry *entry, void *userdata);
typedef void (*OucsHookChunkRead)    (const uint8_t *chunk, size_t size, void *userdata);
typedef void (*OucsHookAfterExtract) (const OucsIndexEntry *entry, void *userdata);
typedef void (*OucsHookOnError)      (OucsError err, const char *msg, void *userdata);

typedef struct {
    OucsHookType type;
    void        *fn;                    /* one of the hook function pointer types above */
    void        *userdata;
} OucsHook;

#define OUCS_MAX_HOOKS 32

/* ─────────────────────────────────────────────────────────────
   MAIN CONTEXT STRUCTS
───────────────────────────────────────────────────────────── */

/* Writer context — used when creating a .oucs file */
typedef struct OucsWriter OucsWriter;

/* Reader context — used when reading/extracting from a .oucs file */
typedef struct OucsReader OucsReader;

/* Stream context — used for chunk-by-chunk streaming */
typedef struct OucsStream OucsStream;

/* ─────────────────────────────────────────────────────────────
   WRITER API
───────────────────────────────────────────────────────────── */

/**
 * Create a new .oucs writer context.
 * @param path     Output file path
 * @param flags    OUCS_FLAG_* bitmask
 * @return         Heap-allocated writer, or NULL on error
 */
OucsWriter *oucs_writer_create(const char *path, uint8_t flags);

/**
 * Set container-level metadata (theme, description, logo).
 */
int oucs_writer_set_container_meta(OucsWriter *w, const OucsContainerMeta *meta);

/**
 * Add a song from a file path. Reads the file, embeds it.
 * @param entry_out  Populated index entry (caller may modify before finalize)
 */
int oucs_writer_add_song(OucsWriter *w, const char *audio_path,
                         const char *name, const char *description,
                         OucsIndexEntry *entry_out);

/**
 * Add a song from a memory buffer.
 */
int oucs_writer_add_song_mem(OucsWriter *w, const uint8_t *data, size_t size,
                              const char *audio_format, const char *name,
                              const char *description, OucsIndexEntry *entry_out);

/**
 * Attach lyrics to the last added song.
 */
int oucs_writer_set_lyrics(OucsWriter *w, const OucsLyrics *lyrics);

/**
 * Attach chapters to the last added song.
 */
int oucs_writer_set_chapters(OucsWriter *w, const OucsChapters *chapters);

/**
 * Attach accessibility metadata to the last added song.
 */
int oucs_writer_set_accessibility(OucsWriter *w, const OucsAccessibility *acc);

/**
 * Enable encryption for the last added song (password).
 */
int oucs_writer_encrypt_song(OucsWriter *w, const char *password);

/**
 * Register a hook callback.
 */
int oucs_writer_register_hook(OucsWriter *w, OucsHookType type, void *fn, void *userdata);

/**
 * Finalize and write the .oucs file to disk.
 * Computes all checksums, ECC, waveforms (if enabled), writes index.
 */
int oucs_writer_finalize(OucsWriter *w);

/**
 * Free writer context and all associated memory.
 */
void oucs_writer_free(OucsWriter *w);

/* ─────────────────────────────────────────────────────────────
   READER API
───────────────────────────────────────────────────────────── */

/**
 * Open a .oucs file for reading.
 */
OucsReader *oucs_reader_open(const char *path);

/**
 * Open a .oucs file from a URL (HTTP Range request based).
 */
OucsReader *oucs_reader_open_url(const char *url);

/**
 * Get the number of songs in the container.
 */
int oucs_reader_song_count(const OucsReader *r);

/**
 * Get container metadata.
 */
int oucs_reader_get_container_meta(const OucsReader *r, OucsContainerMeta *meta_out);

/**
 * Get index entry for song at position idx.
 */
int oucs_reader_get_song_info(const OucsReader *r, uint32_t idx, OucsIndexEntry *entry_out);

/**
 * Find song index by UUID.
 */
int oucs_reader_find_by_uuid(const OucsReader *r, const OucsUUID uuid, uint32_t *idx_out);

/**
 * Extract a song to a file. Verifies CRC and applies ECC correction.
 */
int oucs_reader_extract_song(OucsReader *r, uint32_t idx, const char *out_path,
                              const char *password);

/**
 * Extract a song to a memory buffer. Caller must free *data_out.
 */
int oucs_reader_extract_song_mem(OucsReader *r, uint32_t idx, uint8_t **data_out,
                                  size_t *size_out, const char *password);

/**
 * Get lyrics for song at idx.
 */
int oucs_reader_get_lyrics(const OucsReader *r, uint32_t idx, OucsLyrics *lyrics_out);

/**
 * Get lyric line at a specific timestamp (ms).
 */
int oucs_reader_get_lyric_at_time(const OucsReader *r, uint32_t idx,
                                   uint32_t timestamp_ms, const char **text_out);

/**
 * Get chapters for song at idx.
 */
int oucs_reader_get_chapters(const OucsReader *r, uint32_t idx, OucsChapters *chapters_out);

/**
 * Get waveform data for song at idx.
 */
int oucs_reader_get_waveform(const OucsReader *r, uint32_t idx, OucsWaveform *waveform_out);

/**
 * Get fingerprint for song at idx.
 */
int oucs_reader_get_fingerprint(const OucsReader *r, uint32_t idx,
                                 OucsFingerprint *fp_out);

/**
 * Get accessibility metadata for song at idx.
 */
int oucs_reader_get_accessibility(const OucsReader *r, uint32_t idx,
                                   OucsAccessibility *acc_out);

/**
 * Find duplicate songs by fingerprint comparison.
 * @param matches_out  Array of index pairs [a, b] — caller must free
 * @param count_out    Number of duplicate pairs found
 */
int oucs_reader_find_duplicates(const OucsReader *r, uint32_t **matches_out,
                                 uint32_t *count_out);

/**
 * Get sync manifest.
 */
int oucs_reader_get_sync_manifest(const OucsReader *r, OucsSyncManifest *manifest_out);

/**
 * Get version history.
 */
int oucs_reader_get_history(const OucsReader *r, OucsHistory *history_out);

/**
 * Close reader and free all resources.
 */
void oucs_reader_free(OucsReader *r);

/* ─────────────────────────────────────────────────────────────
   STREAMING API
───────────────────────────────────────────────────────────── */

/**
 * Open a stream for song at idx. Does NOT load the full song.
 * Only the chunk being read is in memory at any time.
 */
OucsStream *oucs_stream_open(OucsReader *r, uint32_t idx,
                              size_t chunk_size, const char *password);

/**
 * Read the next chunk. Returns bytes read (0 = end of stream).
 */
int oucs_stream_read_chunk(OucsStream *s, uint8_t *buf, size_t buf_size,
                            size_t *bytes_read);

/**
 * Seek to a byte position within the song's audio data.
 */
int oucs_stream_seek(OucsStream *s, uint64_t byte_offset);

/**
 * Seek to a chapter by index.
 */
int oucs_stream_seek_chapter(OucsStream *s, uint32_t chapter_idx);

/**
 * Get current byte position within song.
 */
uint64_t oucs_stream_tell(const OucsStream *s);

/**
 * Register a hook on this stream (e.g., OUCS_HOOK_CHUNK_READ).
 */
int oucs_stream_register_hook(OucsStream *s, OucsHookType type, void *fn, void *userdata);

/**
 * Close stream and free resources.
 */
void oucs_stream_free(OucsStream *s);

/* ─────────────────────────────────────────────────────────────
   MERGE & SPLIT API
───────────────────────────────────────────────────────────── */

/**
 * Merge two or more .oucs files into one output file.
 * @param inputs      Array of input file paths
 * @param input_count Number of input files
 * @param output      Output file path
 */
int oucs_merge(const char **inputs, uint32_t input_count, const char *output);

/**
 * Split a .oucs file: extract songs [from_idx, to_idx] into a new file.
 */
int oucs_split(const char *input, uint32_t from_idx, uint32_t to_idx,
               const char *output);

/* ─────────────────────────────────────────────────────────────
   APPEND API
───────────────────────────────────────────────────────────── */

/**
 * Append a song to an existing .oucs file (no full rewrite).
 * Updates version history if present.
 */
int oucs_append_song(const char *oucs_path, const char *audio_path,
                     const char *name, const char *description);

/* ─────────────────────────────────────────────────────────────
   UTILITY API
───────────────────────────────────────────────────────────── */

/**
 * Generate a new UUID v4 (random).
 */
void oucs_uuid_generate(OucsUUID out);

/**
 * Format UUID as string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
 * @param buf  Must be at least 37 bytes
 */
void oucs_uuid_to_str(const OucsUUID uuid, char *buf);

/**
 * Parse UUID string into bytes.
 */
int oucs_uuid_from_str(const char *str, OucsUUID out);

/**
 * Get human-readable error string.
 */
const char *oucs_strerror(OucsError err);

/**
 * Get liboucs version string.
 */
const char *oucs_version(void);

/**
 * Free heap-allocated OucsLyrics members.
 */
void oucs_lyrics_free(OucsLyrics *lyrics);

/**
 * Free heap-allocated OucsChapters members.
 */
void oucs_chapters_free(OucsChapters *chapters);

/**
 * Free heap-allocated OucsAccessibility members.
 */
void oucs_accessibility_free(OucsAccessibility *acc);

/**
 * Free heap-allocated OucsWaveform members.
 */
void oucs_waveform_free(OucsWaveform *wf);

/**
 * Free heap-allocated OucsFingerprint members.
 */
void oucs_fingerprint_free(OucsFingerprint *fp);

/**
 * Free heap-allocated OucsSyncManifest members.
 */
void oucs_sync_manifest_free(OucsSyncManifest *m);

/**
 * Free heap-allocated OucsHistory members.
 */
void oucs_history_free(OucsHistory *h);

/**
 * Free heap-allocated OucsContainerMeta logo.
 */
void oucs_container_meta_free(OucsContainerMeta *meta);

#ifdef __cplusplus
}
#endif

#endif /* OUCS_FORMAT_H */
