/**
 * OUCS Engine - Delta Packing & Deduplication
 * oucs_delta.c
 *
 * Implements:
 *   - Exact duplicate detection via CRC-32 + size comparison
 *   - Reference-pointer deduplication (same song stored once)
 *   - Simple binary delta encoding for near-duplicate audio
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────────────────────
   DELTA ENCODING
   Format: series of (op, length, [data]) tuples
   op 0x00 = COPY: next 4 bytes = offset in reference, next 2 = length
   op 0x01 = INSERT: next 2 bytes = length, then data bytes
───────────────────────────────────────────────────────────── */

#define DELTA_COPY   0x00
#define DELTA_INSERT 0x01
#define DELTA_HASH_BITS 12
#define DELTA_HASH_SIZE (1 << DELTA_HASH_BITS)
#define DELTA_BLOCK_SIZE 64   /* compare 64-byte blocks */

typedef struct {
    uint32_t offset;
} DeltaHashEntry;

static uint32_t delta_hash(const uint8_t *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h & (DELTA_HASH_SIZE - 1);
}

/**
 * Compute delta of `new_data` against `ref_data`.
 * Output is a compact binary diff. Caller must free *delta_out.
 * Returns OUCS_OK or error code.
 */
int oucs_delta_encode(const uint8_t *ref_data, size_t ref_size,
                       const uint8_t *new_data, size_t new_size,
                       uint8_t **delta_out, size_t *delta_size_out) {
    if (!ref_data || !new_data || !delta_out || !delta_size_out)
        return OUCS_ERR_NULL_PARAM;

    /* Build hash table of reference 64-byte blocks */
    DeltaHashEntry *htable = (DeltaHashEntry *)calloc(DELTA_HASH_SIZE, sizeof(DeltaHashEntry));
    if (!htable) return OUCS_ERR_NOMEM;

    for (size_t i = 0; i + DELTA_BLOCK_SIZE <= ref_size; i += DELTA_BLOCK_SIZE) {
        uint32_t h = delta_hash(ref_data + i, DELTA_BLOCK_SIZE);
        htable[h].offset = (uint32_t)i;
    }

    /* Allocate output buffer (worst case = all INSERTs = ~1.5x input) */
    size_t cap = new_size + new_size / 2 + 64;
    uint8_t *out = (uint8_t *)malloc(cap);
    if (!out) { free(htable); return OUCS_ERR_NOMEM; }

    /* Write header: magic "OUCD" + ref_size(4) + new_size(4) */
    size_t pos = 0;
    out[pos++] = 'O'; out[pos++] = 'U'; out[pos++] = 'C'; out[pos++] = 'D';
    out[pos++] = (uint8_t)(ref_size);
    out[pos++] = (uint8_t)(ref_size >> 8);
    out[pos++] = (uint8_t)(ref_size >> 16);
    out[pos++] = (uint8_t)(ref_size >> 24);
    out[pos++] = (uint8_t)(new_size);
    out[pos++] = (uint8_t)(new_size >> 8);
    out[pos++] = (uint8_t)(new_size >> 16);
    out[pos++] = (uint8_t)(new_size >> 24);

    size_t src = 0;
    while (src < new_size) {
        if (src + DELTA_BLOCK_SIZE <= new_size) {
            uint32_t h = delta_hash(new_data + src, DELTA_BLOCK_SIZE);
            uint32_t ref_off = htable[h].offset;
            /* Verify block match */
            if (ref_off + DELTA_BLOCK_SIZE <= ref_size &&
                memcmp(new_data + src, ref_data + ref_off, DELTA_BLOCK_SIZE) == 0) {
                /* COPY instruction */
                if (pos + 7 >= cap) {
                    cap = cap * 2;
                    uint8_t *tmp = (uint8_t *)realloc(out, cap);
                    if (!tmp) { free(out); free(htable); return OUCS_ERR_NOMEM; }
                    out = tmp;
                }
                out[pos++] = DELTA_COPY;
                out[pos++] = (uint8_t)(ref_off);
                out[pos++] = (uint8_t)(ref_off >> 8);
                out[pos++] = (uint8_t)(ref_off >> 16);
                out[pos++] = (uint8_t)(ref_off >> 24);
                out[pos++] = (uint8_t)(DELTA_BLOCK_SIZE);
                out[pos++] = (uint8_t)(DELTA_BLOCK_SIZE >> 8);
                src += DELTA_BLOCK_SIZE;
                continue;
            }
        }

        /* INSERT: find run of non-matching bytes */
        size_t insert_start = src;
        size_t insert_len   = 0;
        while (src + insert_len < new_size && insert_len < 65535) {
            if (src + insert_len + DELTA_BLOCK_SIZE <= new_size) {
                uint32_t h2 = delta_hash(new_data + src + insert_len, DELTA_BLOCK_SIZE);
                uint32_t ro = htable[h2].offset;
                if (ro + DELTA_BLOCK_SIZE <= ref_size &&
                    memcmp(new_data + src + insert_len, ref_data + ro, DELTA_BLOCK_SIZE) == 0)
                    break;
            }
            insert_len++;
        }
        if (insert_len == 0) insert_len = 1;

        /* Reallocate if needed */
        if (pos + insert_len + 3 >= cap) {
            cap = cap + insert_len + cap;
            uint8_t *tmp = (uint8_t *)realloc(out, cap);
            if (!tmp) { free(out); free(htable); return OUCS_ERR_NOMEM; }
            out = tmp;
        }
        out[pos++] = DELTA_INSERT;
        out[pos++] = (uint8_t)(insert_len);
        out[pos++] = (uint8_t)(insert_len >> 8);
        memcpy(out + pos, new_data + insert_start, insert_len);
        pos += insert_len;
        src += insert_len;
        (void)insert_start;
    }

    free(htable);
    *delta_out      = out;
    *delta_size_out = pos;
    return OUCS_OK;
}

/**
 * Apply a delta to reconstruct new_data from ref_data.
 * Caller must free *output_out.
 */
int oucs_delta_decode(const uint8_t *ref_data, size_t ref_size,
                       const uint8_t *delta, size_t delta_size,
                       uint8_t **output_out, size_t *output_size_out) {
    if (!ref_data || !delta || !output_out || !output_size_out)
        return OUCS_ERR_NULL_PARAM;
    if (delta_size < 12) return OUCS_ERR_CORRUPT;

    /* Verify magic */
    if (memcmp(delta, "OUCD", 4) != 0) return OUCS_ERR_CORRUPT;
    size_t pos = 4;

    uint32_t stored_ref_size =
        delta[pos] | (delta[pos+1]<<8) | (delta[pos+2]<<16) | (delta[pos+3]<<24);
    pos += 4;
    uint32_t new_size =
        delta[pos] | (delta[pos+1]<<8) | (delta[pos+2]<<16) | (delta[pos+3]<<24);
    pos += 4;

    if (stored_ref_size != ref_size) return OUCS_ERR_CORRUPT;

    uint8_t *out = (uint8_t *)malloc(new_size);
    if (!out) return OUCS_ERR_NOMEM;
    size_t out_pos = 0;

    while (pos < delta_size && out_pos < new_size) {
        uint8_t op = delta[pos++];
        if (op == DELTA_COPY) {
            if (pos + 6 > delta_size) { free(out); return OUCS_ERR_CORRUPT; }
            uint32_t ref_off  = delta[pos] | (delta[pos+1]<<8) | (delta[pos+2]<<16) | ((uint32_t)delta[pos+3]<<24);
            pos += 4;
            uint16_t copy_len = delta[pos] | (delta[pos+1] << 8);
            pos += 2;
            if (ref_off + copy_len > ref_size) { free(out); return OUCS_ERR_CORRUPT; }
            if (out_pos + copy_len > new_size)  { free(out); return OUCS_ERR_CORRUPT; }
            memcpy(out + out_pos, ref_data + ref_off, copy_len);
            out_pos += copy_len;
        } else if (op == DELTA_INSERT) {
            if (pos + 2 > delta_size) { free(out); return OUCS_ERR_CORRUPT; }
            uint16_t ins_len = delta[pos] | (delta[pos+1] << 8);
            pos += 2;
            if (pos + ins_len > delta_size) { free(out); return OUCS_ERR_CORRUPT; }
            if (out_pos + ins_len > new_size) { free(out); return OUCS_ERR_CORRUPT; }
            memcpy(out + out_pos, delta + pos, ins_len);
            out_pos += ins_len;
            pos     += ins_len;
        } else {
            free(out); return OUCS_ERR_CORRUPT;
        }
    }

    *output_out      = out;
    *output_size_out = out_pos;
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   DEDUPLICATION
───────────────────────────────────────────────────────────── */

/**
 * Scan a .oucs file and report duplicate songs (same CRC32 + size).
 * Prints duplicate pairs to stdout.
 * Returns number of duplicate pairs found.
 */
int oucs_dedup_scan(const char *oucs_path) {
    if (!oucs_path) return OUCS_ERR_NULL_PARAM;

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) return OUCS_ERR_IO;

    uint32_t *matches = NULL;
    uint32_t  count   = 0;
    int ret = oucs_reader_find_duplicates(r, &matches, &count);

    if (ret == OUCS_OK && count > 0) {
        printf("Found %u duplicate pair(s):\n", count);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t a = matches[i * 2];
            uint32_t b = matches[i * 2 + 1];
            OucsIndexEntry ea, eb;
            oucs_reader_get_song_info(r, a, &ea);
            oucs_reader_get_song_info(r, b, &eb);
            printf("  [%u] \"%s\" == [%u] \"%s\" (crc32=0x%08X, size=%llu bytes)\n",
                   a, ea.name, b, eb.name,
                   ea.crc32, (unsigned long long)ea.byte_size);
        }
        free(matches);
    } else if (ret == OUCS_OK) {
        printf("No duplicates found.\n");
    }

    oucs_reader_free(r);
    return (ret == OUCS_OK) ? (int)count : ret;
}

/**
 * Estimate space savings from deduplication without modifying the file.
 * Returns saved bytes (0 if no duplicates).
 */
uint64_t oucs_dedup_estimate_savings(const char *oucs_path) {
    if (!oucs_path) return 0;

    OucsReader *r = oucs_reader_open(oucs_path);
    if (!r) return 0;

    uint32_t *matches = NULL;
    uint32_t  count   = 0;
    uint64_t  savings = 0;

    if (oucs_reader_find_duplicates(r, &matches, &count) == OUCS_OK && count > 0) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t b = matches[i * 2 + 1];
            OucsIndexEntry eb;
            oucs_reader_get_song_info(r, b, &eb);
            savings += eb.byte_size;
        }
        free(matches);
    }

    oucs_reader_free(r);
    return savings;
}
