/**
 * OUCS Engine - Internal Header
 * oucs_internal.h
 *
 * Internal struct definitions, forward declarations, and helpers
 * shared across oucs_encoder.c, oucs_decoder.c, oucs_stream.c, etc.
 *
 * NOT part of the public API. Do not include from user code.
 *
 * License: MIT
 */

#ifndef OUCS_INTERNAL_H
#define OUCS_INTERNAL_H

#include "../include/oucs_format.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Internal Reader context ────────────────────────────────── */
struct OucsReader {
    FILE            *fp;                /* NULL if URL-based */
    char             path[4096];
    char             url[4096];
    int              is_url;
    OucsFileHeader   header;
    OucsIndexEntry  *index;             /* heap-allocated, header.song_count entries */
    OucsContainerMeta meta;
    int              meta_loaded;
    OucsHook         hooks[OUCS_MAX_HOOKS];
    uint32_t         hook_count;
};

/* ─── Internal Stream context ────────────────────────────────── */
struct OucsStream {
    OucsReader      *reader;
    uint32_t         song_idx;
    uint64_t         audio_start;       /* absolute file offset */
    uint64_t         audio_size;        /* bytes of audio data */
    uint64_t         pos;               /* current read position within audio */
    size_t           chunk_size;
    char             password[256];
    OucsHook         hooks[OUCS_MAX_HOOKS];
    uint32_t         hook_count;
    /* cached chapter list for seek_chapter */
    OucsChapters     chapters;
    int              chapters_loaded;
};

/* ─── Internal utility functions (implemented in oucs_util.c) ── */

/**
 * Read exactly `size` bytes from fp at absolute file offset `offset`.
 * Seeks to offset first. Does NOT restore fp position.
 */
int oucs_pread(FILE *fp, void *buf, size_t size, uint64_t offset);

/**
 * CRC-32 (ISO 3309) computation.
 */
uint32_t oucs_crc32(const uint8_t *data, size_t len);

/**
 * Validate magic bytes in a header.
 */
int oucs_header_valid(const OucsFileHeader *hdr);

/**
 * Load index table from an open reader into r->index.
 */
int oucs_load_index(OucsReader *r);

/**
 * Load container metadata from an open reader into r->meta.
 */
int oucs_load_container_meta(OucsReader *r);

/**
 * Write uint32_t little-endian to fp.
 */
int oucs_write_u32(FILE *fp, uint32_t val);

/**
 * Write uint64_t little-endian to fp.
 */
int oucs_write_u64(FILE *fp, uint64_t val);

/**
 * Read uint32_t little-endian from fp.
 */
uint32_t oucs_read_u32(FILE *fp);

/**
 * Read uint64_t little-endian from fp.
 */
uint64_t oucs_read_u64(FILE *fp);

#ifdef __cplusplus
}
#endif

#endif /* OUCS_INTERNAL_H */
